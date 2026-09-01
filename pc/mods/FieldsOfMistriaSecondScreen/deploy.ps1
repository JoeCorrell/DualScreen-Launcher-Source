[CmdletBinding()]
param(
    [string] $Endpoint,
    [string] $RemoteGame = '/storage/emulated/0/Games/Fields.of.Mistria.v1.0.4',
    [string] $Adb = 'adb',
    [switch] $Commit,
    [switch] $SelfTest
)

$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSCommandPath
$package = Join-Path $project 'build\package'
$expectedExe = 'aed5b1f5d5cd75afee86f0cf347655a7e62be361d775d6ffc0671b388660ee2e'
$expectedOriginal = '532b231761a6d6c391069865bde37f6d069f3beedbcf3cfbd0e314bf25ca3157'
$ownerText = 'FieldsOfMistriaSecondScreen:v1'
$liveName = 'SDL3.dll'
$originalName = 'SDL3_FieldsOfMistriaOriginal.dll'
$hostName = '.fomds-panel-host.exe'
$legacyHostName = 'FieldsOfMistriaSecondScreenHost.exe'
$configName = 'FieldsOfMistriaSecondScreen.ini'
$ownerName = 'FieldsOfMistriaSecondScreen.owner'
$manifestName = 'FieldsOfMistriaSecondScreen.manifest.json'
$currentLiveTopics = @(
    'session', 'status', 'progression', 'inventory', 'armor', 'hotbar',
    'location', 'player', 'map', 'villagers', 'animals', 'magic', 'almanac',
    'museum', 'renown', 'calendar_events', 'calendar', 'clock', 'weather',
    'quests'
)
$currentActions = @(
    'select_hotbar', 'swap_slots', 'equip_armor', 'unequip_armor',
    'consume_item', 'pin_spell', 'unpin_spell', 'cast_pinned_spell'
)

function Resolve-InstallState {
    param(
        [bool] $Live,
        [bool] $Original,
        [bool] $Config,
        [bool] $Owner,
        [bool] $Manifest,
        [bool] $LiveIsOriginal,
        [bool] $OwnedFilesValid
    )
    if ($Live -and -not $Original -and -not $Config -and -not $Owner -and
        -not $Manifest -and $LiveIsOriginal) {
        return 'Fresh'
    }
    if ($Live -and $Original -and $Config -and $Owner -and $Manifest -and
        -not $LiveIsOriginal -and $OwnedFilesValid) {
        return 'Owned'
    }
    throw 'Refusing unknown, partial, or unowned SDL3 install state'
}

function Find-UnsafeProcessLines {
    param([string] $ProcessTable)
    return @($ProcessTable -split "`n" | Where-Object {
        $_ -match '(?i)(FieldsOfMistria\.exe|FieldsOfMistriaSecondScreenHost\.exe|\.fomds-panel-host\.exe)'
    })
}

function Invoke-SelfTest {
    if ((Resolve-InstallState $true $false $false $false $false $true $false) -ne 'Fresh') {
        throw 'fresh-state classifier failed'
    }
    if ((Resolve-InstallState $true $true $true $true $true $false $true) -ne 'Owned') {
        throw 'owned-state classifier failed'
    }
    $unknowns = @(
        { Resolve-InstallState $true $true $false $false $false $false $false },
        { Resolve-InstallState $true $false $false $false $false $false $false },
        { Resolve-InstallState $true $true $true $true $true $false $false },
        { Resolve-InstallState $false $true $true $true $true $false $true }
    )
    foreach ($probe in $unknowns) {
        $rejected = $false
        try { & $probe | Out-Null } catch { $rejected = $true }
        if (-not $rejected) { throw 'unknown-state classifier failed open' }
    }
    if ((Find-UnsafeProcessLines "1 S sh`n2 S FieldsOfMistria.exe`n3 S unrelated").Count -ne 1 -or
        (Find-UnsafeProcessLines "1 S sh`n2 S .fomds-panel-host.exe`n3 S unrelated").Count -ne 1 -or
        (Find-UnsafeProcessLines "1 S sh`n2 S FieldsOfMistriaSecondScreenHost.exe`n3 S unrelated").Count -ne 1 -or
        (Find-UnsafeProcessLines "1 S wineserver`n2 S FEXInterpreter`n3 S unrelated").Count -ne 0) {
        throw 'runtime-user classifier failed'
    }
    Write-Host 'Deploy self-test passed: owned states and stopped-process gate fail closed.'
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

if ([string]::IsNullOrWhiteSpace($Endpoint)) {
    throw 'Endpoint is required (example: -Endpoint 100.83.63.70:43569)'
}
if (-not $RemoteGame.StartsWith('/') -or $RemoteGame.Length -lt 16 -or
    $RemoteGame.EndsWith('/') -or $RemoteGame.Contains("'") -or
    $RemoteGame.Contains("`n") -or $RemoteGame.Contains("`r") -or
    $RemoteGame.Contains('/../') -or $RemoteGame.Contains('//')) {
    throw "Unsafe remote game path: $RemoteGame"
}
if (-not (Test-Path -LiteralPath $package -PathType Container)) {
    throw "Build package is missing; run build.ps1 first: $package"
}

function Invoke-Adb {
    param([string[]] $Arguments)
    # adb writes normal connect/push/install progress to stderr. PowerShell 5
    # promotes native stderr records to terminating errors under the script's
    # global Stop preference, even when adb exits successfully.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $Adb @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($exitCode -ne 0) {
        throw "adb failed ($($Arguments -join ' ')): $($output -join "`n")"
    }
    return @($output)
}

function Invoke-Remote {
    param([string] $Command)
    # `adb shell sh -c <argument>` is not argument-transparent on Windows: adb
    # rebuilds a remote shell command line and unquoted semicolons can escape
    # the intended `-c` argument. Feed the already validated script through
    # stdin so neither PowerShell nor adb reparses its shell punctuation.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = $Command | & $Adb -s $Endpoint shell sh 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($exitCode -ne 0) {
        throw "remote shell failed: $($output -join "`n")"
    }
    return @($output)
}

function Quote-Remote {
    param([string] $Value)
    if ($Value.Contains("'") -or $Value.Contains("`n") -or $Value.Contains("`r")) {
        throw "Unsafe remote argument: $Value"
    }
    return "'$Value'"
}

function Remote-Path([string] $Name) { return "$RemoteGame/$Name" }

function Test-RemoteFile {
    param([string] $Path)
    $quoted = Quote-Remote $Path
    $answer = (Invoke-Remote "if [ -f $quoted ] && [ ! -L $quoted ]; then echo yes; else echo no; fi") -join "`n"
    return $answer.Trim() -eq 'yes'
}

function Test-RemoteExists {
    param([string] $Path)
    $quoted = Quote-Remote $Path
    $answer = (Invoke-Remote "if [ -e $quoted ] || [ -L $quoted ]; then echo yes; else echo no; fi") -join "`n"
    return $answer.Trim() -eq 'yes'
}

function Get-RemoteHash {
    param([string] $Path)
    if (-not (Test-RemoteFile $Path)) { return $null }
    $quoted = Quote-Remote $Path
    $line = ((Invoke-Remote "sha256sum $quoted") -join "`n").Trim()
    if ($line -notmatch '^([0-9a-fA-F]{64})\s') { throw "Invalid sha256sum output for $Path" }
    return $Matches[1].ToLowerInvariant()
}

function Get-RemoteText {
    param([string] $Path)
    if (-not (Test-RemoteFile $Path)) { return $null }
    return ((Invoke-Remote ("cat " + (Quote-Remote $Path))) -join "`n")
}

function Test-ExactStringArray {
    param($Actual, [string[]] $Expected)
    $items = @($Actual)
    if ($items.Count -ne $Expected.Count) { return $false }
    for ($index = 0; $index -lt $Expected.Count; ++$index) {
        if ($items[$index] -isnot [string] -or
            $items[$index] -cne $Expected[$index]) { return $false }
    }
    return $true
}

function Test-KnownOwnedFeatureSet {
    param($Manifest)
    if ($Manifest.liveGameData -eq $false -and
        $Manifest.actionsEnabled -eq $false) { return $true }
    if ($Manifest.liveGameData -ne $true) { return $false }
    $topics = @($Manifest.liveTopics)
    $actions = @($Manifest.actionsEnabled)
    if ($topics.Count -eq 0 -or $actions.Count -eq 0) { return $false }
    if (@($topics | Select-Object -Unique).Count -ne $topics.Count -or
        @($actions | Select-Object -Unique).Count -ne $actions.Count) {
        return $false
    }
    foreach ($topic in $topics) {
        if ($topic -isnot [string] -or $currentLiveTopics -cnotcontains $topic) {
            return $false
        }
    }
    foreach ($action in $actions) {
        if ($action -isnot [string] -or $currentActions -cnotcontains $action) {
            return $false
        }
    }
    return $true
}

function Assert-LocalPackage {
    $manifestPath = Join-Path $package $manifestName
    foreach ($name in @($liveName, $hostName, $configName, $ownerName, $manifestName)) {
        if (-not (Test-Path -LiteralPath (Join-Path $package $name) -PathType Leaf)) {
            throw "Incomplete local package: $name"
        }
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $localOwner = (Get-Content -LiteralPath (Join-Path $package $ownerName) -Raw).Trim()
    $localProxyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $package $liveName)).Hash.ToLowerInvariant()
    $localHostHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $package $hostName)).Hash.ToLowerInvariant()
    $localConfigHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $package $configName)).Hash.ToLowerInvariant()
    $localOwnerHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $package $ownerName)).Hash.ToLowerInvariant()
    if ($manifest.schema -ne 1 -or $manifest.owner -ne $ownerText -or
        $localOwner -ne $ownerText -or $manifest.targetExecutable -ne 'FieldsOfMistria.exe' -or
        $manifest.targetVersion -ne '1.0.4' -or $manifest.targetSha256 -ne $expectedExe -or
        $manifest.originalSdlName -ne $originalName -or
        $manifest.originalSdlSha256 -ne $expectedOriginal -or
        $manifest.originalExportCount -ne 1271 -or
        $manifest.proxySha256 -ne $localProxyHash -or
        $manifest.panelHostName -ne $hostName -or
        $manifest.panelHostSha256 -ne $localHostHash -or
        $manifest.panelProcess -ne 'isolated-helper' -or
        $manifest.configSha256 -ne $localConfigHash -or
        $manifest.ownerSha256 -ne $localOwnerHash -or
        $manifest.liveGameData -ne $true -or
        -not (Test-ExactStringArray $manifest.liveTopics $currentLiveTopics) -or
        -not (Test-ExactStringArray $manifest.actionsEnabled $currentActions)) {
        throw 'Local package manifest or ownership validation failed'
    }
    return [pscustomobject]@{
        Manifest = $manifest
        ProxyHash = $localProxyHash
        HostHash = $localHostHash
        ConfigHash = $localConfigHash
        OwnerHash = $localOwnerHash
        ManifestHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $manifestPath).Hash.ToLowerInvariant()
    }
}

function Test-OwnedRemoteState {
    param($Observed)
    try {
        $remoteOwner = (Get-RemoteText $Observed.OwnerPath).Trim()
        if ($remoteOwner -ne $ownerText) { return $false }
        $remoteManifest = Get-RemoteText $Observed.ManifestPath | ConvertFrom-Json
        if ($remoteManifest.schema -ne 1 -or $remoteManifest.owner -ne $ownerText -or
            $remoteManifest.targetExecutable -ne 'FieldsOfMistria.exe' -or
            $remoteManifest.targetVersion -ne '1.0.4' -or
            $remoteManifest.targetSha256 -ne $expectedExe -or
            $remoteManifest.originalSdlName -ne $originalName -or
            $remoteManifest.originalSdlSha256 -ne $expectedOriginal -or
            $remoteManifest.originalExportCount -ne 1271 -or
            -not (Test-KnownOwnedFeatureSet $remoteManifest)) {
            return $false
        }
        $declaresHost = -not [string]::IsNullOrWhiteSpace([string]$remoteManifest.panelHostName)
        if ($declaresHost) {
            if ($remoteManifest.panelProcess -ne 'isolated-helper') { return $false }
            if ($remoteManifest.panelHostName -eq $hostName) {
                if (-not $Observed.Host -or $Observed.LegacyHost -or
                    $Observed.HostHash -ne $remoteManifest.panelHostSha256) {
                    return $false
                }
            } elseif ($remoteManifest.panelHostName -eq $legacyHostName) {
                if (-not $Observed.LegacyHost -or $Observed.Host -or
                    $Observed.LegacyHostHash -ne $remoteManifest.panelHostSha256) {
                    return $false
                }
            } else {
                return $false
            }
        } elseif ($Observed.Host -or $Observed.LegacyHost) {
            return $false
        }
        return $Observed.OriginalHash -eq $expectedOriginal -and
            $Observed.LiveHash -eq $remoteManifest.proxySha256 -and
            $Observed.ConfigHash -eq $remoteManifest.configSha256 -and
            $Observed.OwnerHash -eq $remoteManifest.ownerSha256
    } catch {
        return $false
    }
}

function Push-Stage {
    param([string] $LocalName, [string] $RemoteStage, [string] $ExpectedHash)
    if (Test-RemoteExists $RemoteStage) { throw "Refusing pre-existing stage path: $RemoteStage" }
    Invoke-Adb @('-s', $Endpoint, 'push', (Join-Path $package $LocalName), $RemoteStage) | Out-Null
    $actual = Get-RemoteHash $RemoteStage
    if ($actual -ne $ExpectedHash) { throw "Staged hash mismatch for $LocalName" }
}

function Get-RemoteRuntimeUsers {
    $processTable = ((Invoke-Remote 'ps -A 2>/dev/null || ps 2>/dev/null') -join "`n").Trim()
    if ([string]::IsNullOrWhiteSpace($processTable)) {
        throw 'Could not obtain remote process table; refusing to determine deployment safety'
    }
    $hits = @(Find-UnsafeProcessLines $processTable)
    $mapCommand = 'for map in /proc/[0-9]*/maps; do ' +
        'if grep -F -e ' + (Quote-Remote $exePath) + ' -e ' +
        (Quote-Remote $livePath) + ' -e ' +
        (Quote-Remote $hostPath) + ' -e ' +
        (Quote-Remote $legacyHostPath) + ' -e ' +
        (Quote-Remote $originalPath) + ' "$map" >/dev/null 2>&1; then ' +
        'echo "mapped:$map"; fi; done'
    $mapHits = @((Invoke-Remote $mapCommand) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    return @($hits + $mapHits)
}

$local = Assert-LocalPackage
Invoke-Adb @('connect', $Endpoint) | Out-Null
$remoteDirectory = Quote-Remote $RemoteGame
$directoryCheck = ((Invoke-Remote "if [ -d $remoteDirectory ] && [ ! -L $remoteDirectory ]; then echo yes; else echo no; fi") -join "`n").Trim()
if ($directoryCheck -ne 'yes') { throw "Remote game directory is missing or symlinked: $RemoteGame" }

$exePath = Remote-Path 'FieldsOfMistria.exe'
$livePath = Remote-Path $liveName
$originalPath = Remote-Path $originalName
$hostPath = Remote-Path $hostName
$legacyHostPath = Remote-Path $legacyHostName
$configPath = Remote-Path $configName
$ownerPath = Remote-Path $ownerName
$manifestPath = Remote-Path $manifestName
$exeHash = Get-RemoteHash $exePath
if ($exeHash -ne $expectedExe) { throw "Unsupported remote executable SHA256: $exeHash" }

$observed = [pscustomobject]@{
    LivePath = $livePath
    OriginalPath = $originalPath
    HostPath = $hostPath
    LegacyHostPath = $legacyHostPath
    ConfigPath = $configPath
    OwnerPath = $ownerPath
    ManifestPath = $manifestPath
    Live = Test-RemoteFile $livePath
    Original = Test-RemoteFile $originalPath
    Host = Test-RemoteFile $hostPath
    LegacyHost = Test-RemoteFile $legacyHostPath
    Config = Test-RemoteFile $configPath
    Owner = Test-RemoteFile $ownerPath
    Manifest = Test-RemoteFile $manifestPath
    LiveHash = Get-RemoteHash $livePath
    OriginalHash = Get-RemoteHash $originalPath
    HostHash = Get-RemoteHash $hostPath
    LegacyHostHash = Get-RemoteHash $legacyHostPath
    ConfigHash = Get-RemoteHash $configPath
    OwnerHash = Get-RemoteHash $ownerPath
    ManifestHash = Get-RemoteHash $manifestPath
}

# Refuse non-regular entries rather than treating them as absent.
foreach ($path in @($livePath, $originalPath, $hostPath, $legacyHostPath, $configPath, $ownerPath, $manifestPath)) {
    if ((Test-RemoteExists $path) -and -not (Test-RemoteFile $path)) {
        throw "Refusing non-regular or symlinked install path: $path"
    }
}
$ownedValid = $false
if ($observed.Live -and $observed.Original -and $observed.Config -and
    $observed.Owner -and $observed.Manifest) {
    $ownedValid = Test-OwnedRemoteState $observed
}
$state = Resolve-InstallState $observed.Live $observed.Original $observed.Config `
    $observed.Owner $observed.Manifest ($observed.LiveHash -eq $expectedOriginal) $ownedValid
if ($state -eq 'Fresh' -and ($observed.Host -or $observed.LegacyHost)) {
    throw 'Refusing fresh SDL state with an unowned panel-host executable present'
}

Write-Host "Exact target verified: FieldsOfMistria.exe $exeHash"
Write-Host "Install state: $state"
Write-Host "Original SDL3: $expectedOriginal"
Write-Host "Package proxy: $($local.ProxyHash)"
Write-Host "Package host: $($local.HostHash)"
$exactCurrent = $state -eq 'Owned' -and
    $observed.LiveHash -eq $local.ProxyHash -and
    $observed.HostHash -eq $local.HostHash -and
    -not $observed.LegacyHost -and
    $observed.ConfigHash -eq $local.ConfigHash -and
    $observed.OwnerHash -eq $local.OwnerHash -and
    $observed.ManifestHash -eq $local.ManifestHash
$runtimeUsers = @(Get-RemoteRuntimeUsers)
if ($runtimeUsers.Count -eq 0) {
    Write-Host 'Remote target state: STOPPED (no Fields of Mistria process or exact game-file mapping found).'
} else {
    Write-Host 'Remote runtime state: RUNNING/IN USE'
    $runtimeUsers | ForEach-Object { Write-Host "  $_" }
}
if ($exactCurrent) {
    Write-Host 'Owned installation already contains the exact current audited native package; verified no-op.'
    Write-Host 'No remote files were changed.'
    exit 0
}
if (-not $Commit) {
    Write-Host 'DRY RUN ONLY: no files were pushed, renamed, copied, removed, or replaced.'
    Write-Host 'Re-run with the same explicit endpoint/path and -Commit to install.'
    exit 0
}
if ($runtimeUsers.Count -ne 0) {
    throw 'Refusing commit while Fields of Mistria or its target files are in use; stop the game and rerun'
}
# Recheck at the last read-only boundary immediately before any push/rename.
$runtimeUsers = @(Get-RemoteRuntimeUsers)
if ($runtimeUsers.Count -ne 0) {
    throw 'Refusing commit because Fields of Mistria started after the first process check'
}

$stageTag = ".FieldsOfMistriaSecondScreen.stage.$PID"
$stages = [ordered]@{
    $liveName = Remote-Path "$stageTag.$liveName"
    $hostName = Remote-Path "$stageTag.$hostName"
    $configName = Remote-Path "$stageTag.$configName"
    $ownerName = Remote-Path "$stageTag.$ownerName"
    $manifestName = Remote-Path "$stageTag.$manifestName"
}
$expectedStages = @{
    $liveName = $local.ProxyHash
    $hostName = $local.HostHash
    $configName = $local.ConfigHash
    $ownerName = $local.OwnerHash
    $manifestName = $local.ManifestHash
}

$backupDirectory = $null
$originalMoved = $false
$publishedNames = [System.Collections.Generic.List[string]]::new()
try {
    foreach ($name in $stages.Keys) {
        Push-Stage $name $stages[$name] $expectedStages[$name]
    }
    if ($state -eq 'Fresh') {
        Invoke-Remote ("mv -- " + (Quote-Remote $livePath) + ' ' + (Quote-Remote $originalPath)) | Out-Null
        $originalMoved = $true
        if ((Get-RemoteHash $originalPath) -ne $expectedOriginal -or (Test-RemoteExists $livePath)) {
            throw 'Original SDL rename verification failed'
        }
    } else {
        $stamp = [DateTimeOffset]::UtcNow.ToString('yyyyMMddTHHmmssZ')
        $backupDirectory = Remote-Path ".FieldsOfMistriaSecondScreen.backup.$stamp"
        if (Test-RemoteExists $backupDirectory) { throw "Backup path already exists: $backupDirectory" }
        Invoke-Remote ("mkdir -- " + (Quote-Remote $backupDirectory)) | Out-Null
        foreach ($name in @($liveName, $hostName, $legacyHostName, $configName, $ownerName, $manifestName)) {
            $source = Remote-Path $name
            if (Test-RemoteFile $source) {
                Invoke-Remote ("cp -- " + (Quote-Remote $source) + ' ' +
                    (Quote-Remote "$backupDirectory/$name")) | Out-Null
            }
        }
        if ((Get-RemoteHash "$backupDirectory/$liveName") -ne $observed.LiveHash -or
            ($observed.Host -and (Get-RemoteHash "$backupDirectory/$hostName") -ne $observed.HostHash) -or
            ($observed.LegacyHost -and (Get-RemoteHash "$backupDirectory/$legacyHostName") -ne $observed.LegacyHostHash) -or
            (Get-RemoteHash "$backupDirectory/$configName") -ne $observed.ConfigHash -or
            (Get-RemoteHash "$backupDirectory/$ownerName") -ne $observed.OwnerHash -or
            (Get-RemoteHash "$backupDirectory/$manifestName") -ne $observed.ManifestHash) {
            throw 'Owned backup verification failed'
        }
    }

    foreach ($name in @($liveName, $hostName, $configName, $ownerName, $manifestName)) {
        # Record intent before mv so an adb transport error after a successful
        # remote rename is still recoverable.
        $publishedNames.Add($name)
        Invoke-Remote ("mv -- " + (Quote-Remote $stages[$name]) + ' ' +
            (Quote-Remote (Remote-Path $name))) | Out-Null
    }
    if ((Get-RemoteHash $livePath) -ne $local.ProxyHash -or
        (Get-RemoteHash $hostPath) -ne $local.HostHash -or
        (Get-RemoteHash $originalPath) -ne $expectedOriginal -or
        (Get-RemoteHash $configPath) -ne $local.ConfigHash -or
        (Get-RemoteHash $ownerPath) -ne $local.OwnerHash -or
        (Get-RemoteHash $manifestPath) -ne $local.ManifestHash) {
        throw 'Installed-file verification failed'
    }
    if ($observed.LegacyHost) {
        if ((Get-RemoteHash $legacyHostPath) -ne $observed.LegacyHostHash) {
            throw 'Legacy panel host changed after backup; refusing migration cleanup'
        }
        Invoke-Remote ("rm -f -- " + (Quote-Remote $legacyHostPath)) | Out-Null
        if (Test-RemoteExists $legacyHostPath) {
            throw 'Legacy panel host removal verification failed'
        }
    }
    Write-Host 'Transactional install verified.'
    if ($backupDirectory) { Write-Host "Previous owned package retained at: $backupDirectory" }
} catch {
    $failure = $_
    Write-Warning "Install failed; attempting explicit rollback: $failure"
    foreach ($name in $stages.Keys) {
        if (Test-RemoteExists $stages[$name]) {
            Invoke-Remote ("rm -f -- " + (Quote-Remote $stages[$name])) | Out-Null
        }
    }
    if ($state -eq 'Fresh') {
        if (-not $originalMoved -and (Get-RemoteHash $originalPath) -eq $expectedOriginal -and
            -not (Test-RemoteExists $livePath)) {
            # The remote mv may have completed before adb reported failure.
            $originalMoved = $true
        }
        foreach ($name in $publishedNames) {
            $path = Remote-Path $name
            if (Test-RemoteExists $path) {
                $actual = Get-RemoteHash $path
                if ($actual -eq $expectedStages[$name]) {
                    Invoke-Remote ("rm -f -- " + (Quote-Remote $path)) | Out-Null
                } else {
                    throw "Rollback refused unexpected file at $path; original remains $originalPath"
                }
            }
        }
        if ($originalMoved -and (Test-RemoteFile $originalPath) -and
            -not (Test-RemoteExists $livePath)) {
            Invoke-Remote ("mv -- " + (Quote-Remote $originalPath) + ' ' + (Quote-Remote $livePath)) | Out-Null
        }
        if ((Get-RemoteHash $livePath) -ne $expectedOriginal -or
            (Test-RemoteExists $originalPath)) {
            throw "Fresh-install rollback verification failed; inspect $RemoteGame"
        }
    } elseif ($backupDirectory -and (Test-RemoteExists $backupDirectory)) {
        foreach ($name in @($liveName, $hostName, $legacyHostName, $configName, $ownerName, $manifestName)) {
            $backup = "$backupDirectory/$name"
            if (Test-RemoteFile $backup) {
                Invoke-Remote ("cp -- " + (Quote-Remote $backup) + ' ' +
                (Quote-Remote (Remote-Path $name))) | Out-Null
            } elseif ($name -eq $hostName -and -not $observed.Host -and
                (Get-RemoteHash $hostPath) -eq $local.HostHash) {
                Invoke-Remote ("rm -f -- " + (Quote-Remote $hostPath)) | Out-Null
            }
        }
        if ((Get-RemoteHash $livePath) -ne $observed.LiveHash -or
            (Get-RemoteHash $originalPath) -ne $expectedOriginal -or
            (Get-RemoteHash $hostPath) -ne $observed.HostHash -or
            (Get-RemoteHash $legacyHostPath) -ne $observed.LegacyHostHash -or
            (Get-RemoteHash $configPath) -ne $observed.ConfigHash -or
            (Get-RemoteHash $ownerPath) -ne $observed.OwnerHash -or
            (Get-RemoteHash $manifestPath) -ne $observed.ManifestHash) {
            throw "Owned-install rollback verification failed; backup retained at $backupDirectory"
        }
    }
    throw $failure
}

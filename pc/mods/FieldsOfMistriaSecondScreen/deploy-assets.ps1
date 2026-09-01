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
$packageDirectory = Join-Path $project 'build\gml-patch'
$localArchivePath = Join-Path $packageDirectory 'assets.zip'
$localPatchManifestPath = Join-Path $packageDirectory 'assets.zip.manifest.json'

$assetName = 'assets.zip'
$backupName = 'assets.zip.fomds-original-e55ef6bcaa743000'
$ownerName = 'FieldsOfMistriaSecondScreen.assets.owner'
$deploymentManifestName = 'FieldsOfMistriaSecondScreen.assets.manifest.json'
$ownerText = 'FieldsOfMistriaSecondScreen:assets:v1'

$expectedExeHash = 'aed5b1f5d5cd75afee86f0cf347655a7e62be361d775d6ffc0671b388660ee2e'
$expectedOriginalHash = 'e55ef6bcaa7430008a6fcae7980ef06ca6912bb10431c177807b366a0fc5a450'
$expectedOriginalSize = 625619136L
$expectedOriginalEntries = 122403
$expectedPatchedHash = '8faf44883c3756d317bed2ddaf1881c79c85402a67374e7dcccbc54998667b59'
$expectedPatchedSize = 625762141L
$expectedPatchedEntries = 122404
$expectedPatchManifestHash = 'd86ea8a05427a3512fa785b0562c1df05f2a5df63310435546494cd1f264361f'
$expectedGameOriginalHash = '82cccb93e460246b52033b9dc56a76da5460dc27133150bcbfb40a7bdcbb43e8'
$expectedGamePatchedHash = '9a14923fafb91a3a79cf2e0724445baaa958ee632c75c1fc7f9d51d184997293'
$expectedBridgeHash = '1f0d300410e9f5d1358e9133a2cf0dcab56920f7adf1e989778c73bd638830b2'
$spaceMarginBytes = 64L * 1024L * 1024L

function Resolve-AssetInstallState {
    param(
        [bool] $Live,
        [bool] $Backup,
        [bool] $Owner,
        [bool] $Manifest,
        [bool] $LiveIsOriginal,
        [bool] $BackupIsOriginal,
        [bool] $LiveMatchesOwnedManifest,
        [bool] $MarkersValid
    )

    if ($Live -and -not $Backup -and -not $Owner -and -not $Manifest -and
        $LiveIsOriginal) {
        return 'Fresh'
    }
    if ($Live -and $Backup -and $Owner -and $Manifest -and
        -not $LiveIsOriginal -and $BackupIsOriginal -and
        $LiveMatchesOwnedManifest -and $MarkersValid) {
        return 'Owned'
    }
    throw 'Refusing unknown, partial, symlinked, or unowned assets.zip install state'
}

function Find-UnsafeProcessLines {
    param([string] $ProcessTable)
    return @($ProcessTable -split "`n" | Where-Object {
        $_ -match '(?i)FieldsOfMistria\.exe'
    })
}

function Invoke-SelfTest {
    if ((Resolve-AssetInstallState $true $false $false $false $true $false $false $false) -ne 'Fresh') {
        throw 'fresh-state classifier failed'
    }
    if ((Resolve-AssetInstallState $true $true $true $true $false $true $true $true) -ne 'Owned') {
        throw 'owned-state classifier failed'
    }

    $unknowns = @(
        { Resolve-AssetInstallState $true $true $false $false $true $true $false $false },
        { Resolve-AssetInstallState $true $false $true $true $true $false $false $false },
        { Resolve-AssetInstallState $true $true $true $true $false $false $true $true },
        { Resolve-AssetInstallState $true $true $true $true $false $true $false $true },
        { Resolve-AssetInstallState $true $true $true $true $false $true $true $false },
        { Resolve-AssetInstallState $false $true $true $true $false $true $true $true },
        { Resolve-AssetInstallState $true $false $false $false $false $false $false $false }
    )
    foreach ($probe in $unknowns) {
        $rejected = $false
        try { & $probe | Out-Null } catch { $rejected = $true }
        if (-not $rejected) { throw 'unknown-state classifier failed open' }
    }

    if ((Find-UnsafeProcessLines "1 S sh`n2 S FieldsOfMistria.exe`n3 S unrelated").Count -ne 1 -or
        (Find-UnsafeProcessLines "1 S wineserver`n2 S FEXInterpreter`n3 S unrelated").Count -ne 0) {
        throw 'runtime classifier failed'
    }
    Write-Host 'Asset deployment self-test passed: Fresh/Owned states fail closed.'
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

if ([string]::IsNullOrWhiteSpace($Endpoint)) {
    throw 'Endpoint is required (example: -Endpoint 100.83.63.70:43569)'
}
if ([string]::IsNullOrWhiteSpace($Adb)) {
    throw 'Adb must name an executable or explicit path'
}
if (-not $RemoteGame.StartsWith('/') -or $RemoteGame.Length -lt 16 -or
    $RemoteGame.EndsWith('/') -or $RemoteGame.Contains("'") -or
    $RemoteGame.Contains("`n") -or $RemoteGame.Contains("`r") -or
    $RemoteGame.Contains('/../') -or $RemoteGame.Contains('//')) {
    throw "Unsafe remote game path: $RemoteGame"
}

function Get-LowerFileHash {
    param([string] $Path)
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-StreamHash {
    param([System.IO.Stream] $Stream)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $algorithm.ComputeHash($Stream)
        return ([BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

function Assert-LocalPatchPackage {
    if (-not (Test-Path -LiteralPath $localArchivePath -PathType Leaf)) {
        throw "Staged patched archive is missing: $localArchivePath"
    }
    if (-not (Test-Path -LiteralPath $localPatchManifestPath -PathType Leaf)) {
        throw "Staged patch manifest is missing: $localPatchManifestPath"
    }

    $archiveInfo = Get-Item -LiteralPath $localArchivePath
    $archiveHash = Get-LowerFileHash $localArchivePath
    $patchManifestHash = Get-LowerFileHash $localPatchManifestPath
    if ($archiveInfo.Length -ne $expectedPatchedSize -or $archiveHash -ne $expectedPatchedHash) {
        throw "Local staged archive identity mismatch: $($archiveInfo.Length) bytes / $archiveHash"
    }
    if ($patchManifestHash -ne $expectedPatchManifestHash) {
        throw "Local patch manifest hash mismatch: $patchManifestHash"
    }

    $patchManifest = Get-Content -LiteralPath $localPatchManifestPath -Raw | ConvertFrom-Json
    if ($patchManifest.tool_schema -ne 1 -or $patchManifest.mode -ne 'build' -or
        $patchManifest.input_sha256 -ne $expectedOriginalHash -or
        $patchManifest.input_size -ne $expectedOriginalSize -or
        $patchManifest.input_entries -ne $expectedOriginalEntries -or
        $patchManifest.patched_archive_sha256 -ne $expectedPatchedHash -or
        $patchManifest.patched_archive_size -ne $expectedPatchedSize -or
        $patchManifest.output_entry_count -ne $expectedPatchedEntries -or
        $patchManifest.game_original_sha256 -ne $expectedGameOriginalHash -or
        $patchManifest.game_patched_sha256 -ne $expectedGamePatchedHash -or
        $patchManifest.bridge_sha256 -ne $expectedBridgeHash -or
        $patchManifest.new_entry_compression -ne 'stored' -or
        $patchManifest.full_crc_verified -ne $true -or
        $patchManifest.untouched_local_records_and_compressed_data_preserved -ne $true -or
        $patchManifest.untouched_central_records_preserved -ne $true -or
        $patchManifest.required_backup_name -ne $backupName -or
        $patchManifest.install_contract.source_sha256_must_equal -ne $expectedOriginalHash -or
        $patchManifest.install_contract.create_verified_backup_before_replace -ne $true -or
        $patchManifest.install_contract.never_overwrite_the_only_original -ne $true -or
        $patchManifest.install_contract.install_by_same_directory_atomic_rename -ne $true -or
        $patchManifest.install_contract.rollback_restores_the_verified_original -ne $true) {
        throw 'Local patch manifest proof does not match the final audited v1.0.4 package'
    }

    Add-Type -AssemblyName System.IO.Compression
    $fileStream = [System.IO.File]::OpenRead($localArchivePath)
    try {
        $archive = [System.IO.Compression.ZipArchive]::new(
            $fileStream,
            [System.IO.Compression.ZipArchiveMode]::Read,
            $false
        )
        try {
            if ($archive.Entries.Count -ne $expectedPatchedEntries) {
                throw "Local archive entry-count mismatch: $($archive.Entries.Count)"
            }
            $gameEntries = @($archive.Entries | Where-Object { $_.FullName -eq 'assets/gml/objects/Game.gml' })
            $bridgeEntries = @($archive.Entries | Where-Object { $_.FullName -eq 'assets/gml/scripts/DualScreenBridge.gml' })
            if ($gameEntries.Count -ne 1 -or $bridgeEntries.Count -ne 1) {
                throw 'Local archive does not contain exactly one patched Game and bridge entry'
            }
            if ($gameEntries[0].CompressedLength -ne $gameEntries[0].Length -or
                $bridgeEntries[0].CompressedLength -ne $bridgeEntries[0].Length) {
                throw 'Local patched Game/bridge entries are not ZIP_STORED'
            }
            $gameStream = $gameEntries[0].Open()
            try { $gameHash = Get-StreamHash $gameStream } finally { $gameStream.Dispose() }
            $bridgeStream = $bridgeEntries[0].Open()
            try { $bridgeHash = Get-StreamHash $bridgeStream } finally { $bridgeStream.Dispose() }
            if ($gameHash -ne $expectedGamePatchedHash -or $bridgeHash -ne $expectedBridgeHash) {
                throw 'Local patched Game/bridge entry payload hash mismatch'
            }
        } finally {
            $archive.Dispose()
        }
    } finally {
        $fileStream.Dispose()
    }

    return [pscustomobject]@{
        ArchiveHash = $archiveHash
        ArchiveSize = $archiveInfo.Length
        ArchiveEntries = $expectedPatchedEntries
        PatchManifestHash = $patchManifestHash
        BridgeHash = $expectedBridgeHash
        GamePatchedHash = $expectedGamePatchedHash
    }
}

function Invoke-Adb {
    param([string[]] $Arguments)
    # Windows PowerShell 5 promotes native stderr (including adb's ordinary
    # push progress) to ErrorRecord objects when the script preference is
    # Stop. Capture both streams, but judge native success by its exit code.
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
    # adb reconstructs shell arguments on Windows. Feed a validated program to
    # stdin so punctuation is interpreted exactly once by the remote shell.
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
    $line = ((Invoke-Remote ("sha256sum " + (Quote-Remote $Path))) -join "`n").Trim()
    if ($line -notmatch '^([0-9a-fA-F]{64})\s') { throw "Invalid sha256sum output for $Path" }
    return $Matches[1].ToLowerInvariant()
}

function Get-RemoteSize {
    param([string] $Path)
    if (-not (Test-RemoteFile $Path)) { return $null }
    $line = ((Invoke-Remote ("stat -c %s " + (Quote-Remote $Path))) -join "`n").Trim()
    $value = 0L
    if (-not [Int64]::TryParse($line, [ref] $value) -or $value -lt 0) {
        throw "Invalid remote size for $Path`: $line"
    }
    return $value
}

function Get-RemoteText {
    param([string] $Path)
    if (-not (Test-RemoteFile $Path)) { return $null }
    return ((Invoke-Remote ("cat " + (Quote-Remote $Path))) -join "`n")
}

function Get-RemoteAvailableBytes {
    $line = @((Invoke-Remote ("df -Pk " + (Quote-Remote $RemoteGame) + ' 2>/dev/null | tail -n 1')) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) | Select-Object -Last 1
    if ($null -eq $line) { throw 'Could not obtain free-space information for the remote game filesystem' }
    $parts = @($line.Trim() -split '\s+')
    if ($parts.Count -lt 5) { throw "Unrecognized remote df output: $line" }
    $availableKiB = 0L
    if (-not [Int64]::TryParse($parts[3], [ref] $availableKiB) -or $availableKiB -lt 0) {
        throw "Unrecognized available-space value: $line"
    }
    return $availableKiB * 1024L
}

$exePath = Remote-Path 'FieldsOfMistria.exe'
$livePath = Remote-Path $assetName
$backupPath = Remote-Path $backupName
$ownerPath = Remote-Path $ownerName
$deploymentManifestPath = Remote-Path $deploymentManifestName

function Assert-ManagedPathsRegularOrAbsent {
    foreach ($path in @($livePath, $backupPath, $ownerPath, $deploymentManifestPath)) {
        if ((Test-RemoteExists $path) -and -not (Test-RemoteFile $path)) {
            throw "Refusing non-regular or symlinked managed path: $path"
        }
    }
}

function Test-OwnedRemoteState {
    param($Observed)
    try {
        if ((Get-RemoteText $ownerPath).Trim() -ne $ownerText) { return $false }
        $manifest = Get-RemoteText $deploymentManifestPath | ConvertFrom-Json
        if ($manifest.schema -ne 1 -or $manifest.owner -ne $ownerText -or
            $manifest.targetExecutable -ne 'FieldsOfMistria.exe' -or
            $manifest.targetVersion -ne '1.0.4' -or
            $manifest.targetExecutableSha256 -ne $expectedExeHash -or
            $manifest.sourceArchiveName -ne $assetName -or
            $manifest.sourceArchiveSha256 -ne $expectedOriginalHash -or
            $manifest.sourceArchiveSize -ne $expectedOriginalSize -or
            $manifest.sourceArchiveEntries -ne $expectedOriginalEntries -or
            $manifest.originalBackupName -ne $backupName -or
            $manifest.patchToolSchema -ne 1 -or
            $manifest.patchManifestSha256 -notmatch '^[0-9a-f]{64}$' -or
            $manifest.patchedArchiveSha256 -notmatch '^[0-9a-f]{64}$' -or
            $manifest.patchedArchiveSize -lt $expectedOriginalSize -or
            $manifest.patchedArchiveEntries -lt ($expectedOriginalEntries + 1) -or
            $manifest.patchedGameSha256 -notmatch '^[0-9a-f]{64}$' -or
            $manifest.bridgeSha256 -notmatch '^[0-9a-f]{64}$' -or
            $manifest.ownerSha256 -ne $Observed.OwnerHash) {
            return $false
        }
        return $Observed.BackupHash -eq $expectedOriginalHash -and
            $Observed.BackupSize -eq $expectedOriginalSize -and
            $Observed.LiveHash -eq $manifest.patchedArchiveSha256 -and
            $Observed.LiveSize -eq $manifest.patchedArchiveSize
    } catch {
        return $false
    }
}

function Get-ObservedState {
    Assert-ManagedPathsRegularOrAbsent
    $observed = [pscustomobject]@{
        Live = Test-RemoteFile $livePath
        Backup = Test-RemoteFile $backupPath
        Owner = Test-RemoteFile $ownerPath
        Manifest = Test-RemoteFile $deploymentManifestPath
        LiveHash = Get-RemoteHash $livePath
        LiveSize = Get-RemoteSize $livePath
        BackupHash = Get-RemoteHash $backupPath
        BackupSize = Get-RemoteSize $backupPath
        OwnerHash = Get-RemoteHash $ownerPath
        ManifestHash = Get-RemoteHash $deploymentManifestPath
    }
    $markersValid = $false
    $liveMatchesOwnedManifest = $false
    if ($observed.Live -and $observed.Backup -and $observed.Owner -and $observed.Manifest) {
        $markersValid = Test-OwnedRemoteState $observed
        $liveMatchesOwnedManifest = $markersValid
    }
    $state = Resolve-AssetInstallState $observed.Live $observed.Backup `
        $observed.Owner $observed.Manifest `
        ($observed.LiveHash -eq $expectedOriginalHash -and $observed.LiveSize -eq $expectedOriginalSize) `
        ($observed.BackupHash -eq $expectedOriginalHash -and $observed.BackupSize -eq $expectedOriginalSize) `
        $liveMatchesOwnedManifest $markersValid
    Add-Member -InputObject $observed -NotePropertyName State -NotePropertyValue $state
    return $observed
}

function Get-RemoteRuntimeUsers {
    $processTable = ((Invoke-Remote 'ps -A 2>/dev/null || ps 2>/dev/null') -join "`n").Trim()
    if ([string]::IsNullOrWhiteSpace($processTable)) {
        throw 'Could not obtain remote process table; refusing deployment safety decision'
    }
    $hits = @(Find-UnsafeProcessLines $processTable)
    $mapProgram = 'for map in /proc/[0-9]*/maps; do ' +
        'if grep -F -e ' + (Quote-Remote $exePath) + ' -e ' +
        (Quote-Remote $livePath) + ' -e ' +
        (Quote-Remote $backupPath) + ' "$map" >/dev/null 2>&1; then ' +
        'echo "mapped:$map"; fi; done'
    $mapHits = @((Invoke-Remote $mapProgram) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    return @($hits + $mapHits)
}

function Assert-ObservedUnchanged {
    param($Before, $After)
    foreach ($field in @('State', 'LiveHash', 'LiveSize', 'BackupHash', 'BackupSize', 'OwnerHash', 'ManifestHash')) {
        if ($Before.$field -ne $After.$field) {
            throw "Remote install state changed during staging ($field)"
        }
    }
}

function Push-Stage {
    param([string] $LocalPath, [string] $RemotePath, [string] $ExpectedHash)
    if (Test-RemoteExists $RemotePath) { throw "Refusing pre-existing stage path: $RemotePath" }
    Invoke-Adb @('-s', $Endpoint, 'push', $LocalPath, $RemotePath) | Out-Null
    if ((Get-RemoteHash $RemotePath) -ne $ExpectedHash) {
        throw "Staged-file hash mismatch: $RemotePath"
    }
}

function Remove-RemoteExact {
    param([string] $Path, [string] $ExpectedHash)
    if (-not (Test-RemoteExists $Path)) { return }
    if (-not (Test-RemoteFile $Path) -or (Get-RemoteHash $Path) -ne $ExpectedHash) {
        throw "Refusing to remove unexpected rollback/stage path: $Path"
    }
    Invoke-Remote ("rm -f -- " + (Quote-Remote $Path)) | Out-Null
}

$local = Assert-LocalPatchPackage
Invoke-Adb @('connect', $Endpoint) | Out-Null
$quotedRemoteGame = Quote-Remote $RemoteGame
$directoryCheck = ((Invoke-Remote "if [ -d $quotedRemoteGame ] && [ ! -L $quotedRemoteGame ]; then echo yes; else echo no; fi") -join "`n").Trim()
if ($directoryCheck -ne 'yes') { throw "Remote game directory is missing or symlinked: $RemoteGame" }

$exeHash = Get-RemoteHash $exePath
if ($exeHash -ne $expectedExeHash) { throw "Unsupported remote executable SHA256: $exeHash" }
$initial = Get-ObservedState
$available = Get-RemoteAvailableBytes
$needed = $local.ArchiveSize + $spaceMarginBytes

Write-Host "Exact target verified: FieldsOfMistria.exe $exeHash"
Write-Host "Asset install state: $($initial.State)"
Write-Host "Original archive: $expectedOriginalHash"
Write-Host "Patched archive:  $($local.ArchiveHash)"
Write-Host "Free/required staging bytes: $available / $needed"
if ($available -lt $needed) {
    throw "Insufficient same-filesystem space to stage assets.zip plus margin ($available < $needed)"
}

$runtimeUsers = @(Get-RemoteRuntimeUsers)
if ($runtimeUsers.Count -eq 0) {
    Write-Host 'Remote target state: STOPPED (no Fields of Mistria process or exact game-file mapping found).'
} else {
    Write-Host 'Remote runtime state: RUNNING/IN USE'
    $runtimeUsers | ForEach-Object { Write-Host "  $_" }
}

$isCurrentOwned = $initial.State -eq 'Owned' -and
    $initial.LiveHash -eq $local.ArchiveHash -and
    $initial.LiveSize -eq $local.ArchiveSize
if ($isCurrentOwned) {
    Write-Host 'Owned installation already contains the exact current audited archive; verified no-op.'
    if (-not $Commit) { Write-Host 'DRY RUN ONLY: no remote files were changed.' }
    exit 0
}

if (-not $Commit) {
    Write-Host 'DRY RUN ONLY: no files were pushed, renamed, copied, removed, or replaced.'
    Write-Host 'Re-run with the same explicit endpoint/path and -Commit to install.'
    exit 0
}
if ($runtimeUsers.Count -ne 0) {
    throw 'Refusing commit while the game/runtime or exact asset archive is in use'
}

$ownerTemp = [System.IO.Path]::GetTempFileName()
$deploymentManifestTemp = [System.IO.Path]::GetTempFileName()
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$stageTag = ".FieldsOfMistriaSecondScreen.assets.stage.$PID"
$archiveStage = Remote-Path "$stageTag.assets.zip"
$ownerStage = Remote-Path "$stageTag.owner"
$manifestStage = Remote-Path "$stageTag.manifest.json"
$oldLiveStage = Remote-Path "$stageTag.previous.assets.zip"
$oldOwnerStage = Remote-Path "$stageTag.previous.owner"
$oldManifestStage = Remote-Path "$stageTag.previous.manifest.json"

$ownerMoved = $false
$manifestMoved = $false
$liveMoved = $false
$newLivePublished = $false
$newOwnerPublished = $false
$newManifestPublished = $false

try {
    [System.IO.File]::WriteAllText($ownerTemp, "$ownerText`n", $utf8NoBom)
    $ownerHash = Get-LowerFileHash $ownerTemp
    $deploymentManifest = [ordered]@{
        schema = 1
        owner = $ownerText
        targetExecutable = 'FieldsOfMistria.exe'
        targetVersion = '1.0.4'
        targetExecutableSha256 = $expectedExeHash
        sourceArchiveName = $assetName
        sourceArchiveSha256 = $expectedOriginalHash
        sourceArchiveSize = $expectedOriginalSize
        sourceArchiveEntries = $expectedOriginalEntries
        originalBackupName = $backupName
        patchToolSchema = 1
        patchManifestSha256 = $local.PatchManifestHash
        patchedArchiveSha256 = $local.ArchiveHash
        patchedArchiveSize = $local.ArchiveSize
        patchedArchiveEntries = $local.ArchiveEntries
        patchedGameSha256 = $local.GamePatchedHash
        bridgeSha256 = $local.BridgeHash
        ownerSha256 = $ownerHash
    }
    $deploymentJson = ($deploymentManifest | ConvertTo-Json -Depth 5) + "`n"
    [System.IO.File]::WriteAllText($deploymentManifestTemp, $deploymentJson, $utf8NoBom)
    $deploymentManifestHash = Get-LowerFileHash $deploymentManifestTemp

    foreach ($stage in @($archiveStage, $ownerStage, $manifestStage,
            $oldLiveStage, $oldOwnerStage, $oldManifestStage)) {
        if (Test-RemoteExists $stage) { throw "Refusing pre-existing transaction path: $stage" }
    }
    Push-Stage $localArchivePath $archiveStage $local.ArchiveHash
    Push-Stage $ownerTemp $ownerStage $ownerHash
    Push-Stage $deploymentManifestTemp $manifestStage $deploymentManifestHash

    # Last read-only boundary: repeat process/maps, executable, state, space,
    # and staged hashes immediately before the first rename.
    $runtimeUsers = @(Get-RemoteRuntimeUsers)
    if ($runtimeUsers.Count -ne 0) {
        throw 'Refusing commit because the game/runtime started during staging'
    }
    if ((Get-RemoteHash $exePath) -ne $expectedExeHash) {
        throw 'Remote executable changed during staging'
    }
    $preCommit = Get-ObservedState
    Assert-ObservedUnchanged $initial $preCommit
    if ((Get-RemoteAvailableBytes) -lt $spaceMarginBytes) {
        throw 'Free space fell below the required rollback margin during staging'
    }
    if ((Get-RemoteHash $archiveStage) -ne $local.ArchiveHash -or
        (Get-RemoteHash $ownerStage) -ne $ownerHash -or
        (Get-RemoteHash $manifestStage) -ne $deploymentManifestHash) {
        throw 'Staged files changed before commit'
    }

    if ($initial.State -eq 'Fresh') {
        $liveMoved = $true
        Invoke-Remote ("mv -- " + (Quote-Remote $livePath) + ' ' + (Quote-Remote $backupPath)) | Out-Null
        if ((Get-RemoteHash $backupPath) -ne $expectedOriginalHash -or (Test-RemoteExists $livePath)) {
            throw 'Atomic original-archive backup rename failed verification'
        }
    } else {
        # Preserve the prior owned generation through the transaction without
        # copying 625 MB. The verified original backup is never moved.
        $liveMoved = $true
        Invoke-Remote ("mv -- " + (Quote-Remote $livePath) + ' ' + (Quote-Remote $oldLiveStage)) | Out-Null
        $ownerMoved = $true
        Invoke-Remote ("mv -- " + (Quote-Remote $ownerPath) + ' ' + (Quote-Remote $oldOwnerStage)) | Out-Null
        $manifestMoved = $true
        Invoke-Remote ("mv -- " + (Quote-Remote $deploymentManifestPath) + ' ' + (Quote-Remote $oldManifestStage)) | Out-Null
    }

    $newLivePublished = $true
    Invoke-Remote ("mv -- " + (Quote-Remote $archiveStage) + ' ' + (Quote-Remote $livePath)) | Out-Null
    $newOwnerPublished = $true
    Invoke-Remote ("mv -- " + (Quote-Remote $ownerStage) + ' ' + (Quote-Remote $ownerPath)) | Out-Null
    $newManifestPublished = $true
    Invoke-Remote ("mv -- " + (Quote-Remote $manifestStage) + ' ' + (Quote-Remote $deploymentManifestPath)) | Out-Null

    if ((Get-RemoteHash $livePath) -ne $local.ArchiveHash -or
        (Get-RemoteSize $livePath) -ne $local.ArchiveSize -or
        (Get-RemoteHash $backupPath) -ne $expectedOriginalHash -or
        (Get-RemoteSize $backupPath) -ne $expectedOriginalSize -or
        (Get-RemoteHash $ownerPath) -ne $ownerHash -or
        (Get-RemoteHash $deploymentManifestPath) -ne $deploymentManifestHash) {
        throw 'Installed asset package failed final verification'
    }
    $installed = Get-ObservedState
    if ($installed.State -ne 'Owned') { throw 'Final install did not classify as Owned' }

    if ($initial.State -eq 'Owned') {
        foreach ($oldEntry in @(
            @($oldLiveStage, $initial.LiveHash),
            @($oldOwnerStage, $initial.OwnerHash),
            @($oldManifestStage, $initial.ManifestHash)
        )) {
            try { Remove-RemoteExact $oldEntry[0] $oldEntry[1] } catch {
                Write-Warning "Installed update is valid; prior owned generation cleanup was retained: $($_.Exception.Message)"
            }
        }
    }
    Write-Host 'Transactional GML asset install verified.'
    Write-Host "Verified original retained at: $backupPath"
} catch {
    $installFailure = $_.Exception.Message
    Write-Warning "Asset install failed; attempting explicit rollback: $installFailure"
    try {
        if ($newManifestPublished) { Remove-RemoteExact $deploymentManifestPath $deploymentManifestHash }
        if ($newOwnerPublished) { Remove-RemoteExact $ownerPath $ownerHash }
        if ($newLivePublished) { Remove-RemoteExact $livePath $local.ArchiveHash }

        if ($initial.State -eq 'Fresh') {
            # Account for a completed remote rename followed by an adb error.
            if (-not $liveMoved -and (Get-RemoteHash $backupPath) -eq $expectedOriginalHash -and
                -not (Test-RemoteExists $livePath)) {
                $liveMoved = $true
            }
            if ($liveMoved -and -not (Test-RemoteExists $livePath) -and
                (Get-RemoteHash $backupPath) -eq $expectedOriginalHash) {
                Invoke-Remote ("mv -- " + (Quote-Remote $backupPath) + ' ' + (Quote-Remote $livePath)) | Out-Null
            }
            if ((Get-RemoteHash $livePath) -ne $expectedOriginalHash -or
                (Get-RemoteSize $livePath) -ne $expectedOriginalSize -or
                (Test-RemoteExists $backupPath) -or (Test-RemoteExists $ownerPath) -or
                (Test-RemoteExists $deploymentManifestPath)) {
                throw 'Fresh-state rollback verification failed'
            }
        } else {
            if (-not $manifestMoved -and (Test-RemoteFile $oldManifestStage) -and
                -not (Test-RemoteExists $deploymentManifestPath)) { $manifestMoved = $true }
            if (-not $ownerMoved -and (Test-RemoteFile $oldOwnerStage) -and
                -not (Test-RemoteExists $ownerPath)) { $ownerMoved = $true }
            if (-not $liveMoved -and (Test-RemoteFile $oldLiveStage) -and
                -not (Test-RemoteExists $livePath)) { $liveMoved = $true }

            if ($liveMoved -and -not (Test-RemoteExists $livePath)) {
                Invoke-Remote ("mv -- " + (Quote-Remote $oldLiveStage) + ' ' + (Quote-Remote $livePath)) | Out-Null
            }
            if ($ownerMoved -and -not (Test-RemoteExists $ownerPath)) {
                Invoke-Remote ("mv -- " + (Quote-Remote $oldOwnerStage) + ' ' + (Quote-Remote $ownerPath)) | Out-Null
            }
            if ($manifestMoved -and -not (Test-RemoteExists $deploymentManifestPath)) {
                Invoke-Remote ("mv -- " + (Quote-Remote $oldManifestStage) + ' ' + (Quote-Remote $deploymentManifestPath)) | Out-Null
            }
            $rolledBack = Get-ObservedState
            Assert-ObservedUnchanged $initial $rolledBack
        }
    } catch {
        throw "Install failed: $installFailure. Rollback also failed: $($_.Exception.Message). The verified original backup must not be removed: $backupPath"
    }
    throw "Install failed and rollback was verified: $installFailure"
} finally {
    foreach ($entry in @(
        @($archiveStage, $local.ArchiveHash),
        @($ownerStage, $ownerHash),
        @($manifestStage, $deploymentManifestHash)
    )) {
        try { Remove-RemoteExact $entry[0] $entry[1] } catch { Write-Warning $_.Exception.Message }
    }
    foreach ($localTemp in @($ownerTemp, $deploymentManifestTemp)) {
        if (Test-Path -LiteralPath $localTemp -PathType Leaf) {
            Remove-Item -LiteralPath $localTemp -Force
        }
    }
}

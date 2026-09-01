[CmdletBinding()]
param(
    [string] $Adb = "C:\Users\Shadow\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string] $Endpoint = "100.83.63.70:34023",
    [string] $RemoteGame = "/storage/emulated/0/Download/Fallout-4-SteamRIP.com/Fallout 4",
    [switch] $Commit
)

$ErrorActionPreference = "Stop"
$expectedExe = "061c1ab3738612dbc46d748f02c27795a1003501c90c15d1e649d8a07fcbbb4e"
$assetRoot = Join-Path $PSScriptRoot "build\generated-assets"
$manifestPath = Join-Path $assetRoot "manifest.ini"
$hashListPath = Join-Path $assetRoot "files.sha256"

if ($RemoteGame -notmatch '^/[A-Za-z0-9._ /-]+$') {
    throw "RemoteGame contains unsupported characters"
}
$remoteSegments = $RemoteGame.Split('/', [StringSplitOptions]::RemoveEmptyEntries)
if ($RemoteGame.EndsWith('/') -or $RemoteGame.Contains('//') -or
    $remoteSegments.Count -lt 2 -or $remoteSegments -contains '.' -or
    $remoteSegments -contains '..') {
    throw "RemoteGame must be a normalized, non-root absolute device path without '.' or '..' segments"
}
if (-not (Test-Path -LiteralPath $Adb -PathType Leaf)) { throw "ADB was not found" }
if (-not (Test-Path -LiteralPath $assetRoot -PathType Container) -or
    -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Generate the user-owned artwork cache first"
}

$manifest = [IO.File]::ReadAllText($manifestPath) -replace "`r", ""
$perkMatch = [regex]::Match($manifest, '(?m)^PerkCount=([0-9]+)$')
if ($manifest -notmatch '(?m)^Format=Fallout4SecondScreenAssetsV1$' -or
    $manifest -notmatch '(?m)^InterfaceBa2Sha256=[0-9a-f]{64}$' -or
    $manifest -notmatch '(?m)^SpecialCount=7$' -or
    $manifest -notmatch '(?m)^ConditionBodyCount=17$' -or
    $manifest -notmatch '(?m)^ConditionHeadCount=32$' -or
    -not $perkMatch.Success -or
    [int]$perkMatch.Groups[1].Value -lt 80) {
    throw "Generated artwork manifest is invalid"
}
$perkCount = [int]$perkMatch.Groups[1].Value

function Test-RgbaFile {
    param([Parameter(Mandatory = $true)][string] $Path)
    $file = Get-Item -LiteralPath $Path
    if ($file.Length -lt 16 -or $file.Length -gt 16MB + 12) { return $false }
    $stream = [IO.File]::OpenRead($file.FullName)
    try {
        $header = New-Object byte[] 12
        if ($stream.Read($header, 0, 12) -ne 12 -or
            [Text.Encoding]::ASCII.GetString($header, 0, 4) -ne 'RGBA') {
            return $false
        }
        $width = [BitConverter]::ToUInt32($header, 4)
        $height = [BitConverter]::ToUInt32($header, 8)
        return $width -gt 0 -and $height -gt 0 -and
            $width -le 2048 -and $height -le 2048 -and
            $file.Length -eq 12 + [uint64]$width * [uint64]$height * 4
    }
    finally { $stream.Dispose() }
}

$special = @(Get-ChildItem -LiteralPath (Join-Path $assetRoot 'special') -Filter '*.rgba' -File)
$bodies = @(Get-ChildItem -LiteralPath (Join-Path $assetRoot 'condition') -Filter 'body_*.rgba' -File)
$heads = @(Get-ChildItem -LiteralPath (Join-Path $assetRoot 'condition') -Filter 'head_*.rgba' -File)
$perks = @(Get-ChildItem -LiteralPath (Join-Path $assetRoot 'perks') -Filter '*.rgba' -File)
if ($special.Count -ne 7 -or $bodies.Count -ne 17 -or $heads.Count -ne 32 -or
    $perks.Count -ne $perkCount) {
    throw "Generated artwork counts do not match the manifest"
}
$rgbaFiles = @($special + $bodies + $heads + $perks)
foreach ($file in $rgbaFiles) {
    if (-not (Test-RgbaFile $file.FullName)) { throw "Invalid RGBA asset: $($file.FullName)" }
}

$ownedFiles = @($rgbaFiles + (Get-Item -LiteralPath $manifestPath)) |
    Sort-Object FullName
$expectedOwnedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$hashLines = foreach ($file in $ownedFiles) {
    $relative = $file.FullName.Substring($assetRoot.Length).TrimStart('\').Replace('\','/')
    if (-not $expectedOwnedPaths.Add($relative)) {
        throw "Duplicate generated artwork path: $relative"
    }
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
}
[IO.File]::WriteAllText($hashListPath, (($hashLines -join "`n") + "`n"),
                        [Text.UTF8Encoding]::new($false))
$expectedRemoteFiles = $ownedFiles.Count + 1
$hashListHash = (Get-FileHash -LiteralPath $hashListPath -Algorithm SHA256).Hash.ToLowerInvariant()

& $Adb connect $Endpoint | Write-Host
if ((& $Adb -s $Endpoint get-state 2>$null) -ne 'device') {
    throw "ADB device $Endpoint is not online"
}
$remoteHashLine = ((& $Adb -s $Endpoint shell "sha256sum '$RemoteGame/Fallout4.exe' 2>/dev/null") -join "`n").Trim()
if (-not $remoteHashLine -or ($remoteHashLine -split '\s+')[0].ToLowerInvariant() -ne $expectedExe) {
    throw "The installed Fallout4.exe is missing or unsupported"
}
$busy = (& $Adb -s $Endpoint shell "ps -A -o PID,NAME,ARGS | grep -i -E 'Fallout|wineserver' | grep -v grep || true") -join "`n"
if ($busy.Trim()) {
    Write-Host $busy -ForegroundColor Yellow
    throw "Exit Fallout and the WEMU Games session normally before artwork deployment"
}

$remoteAssets = "$RemoteGame/Fallout4SecondScreen.assets"
$state = ((& $Adb -s $Endpoint shell "if test ! -e '$remoteAssets'; then printf absent; elif test -d '$remoteAssets' && test ! -L '$remoteAssets'; then printf directory; else printf invalid; fi") -join "`n").Trim()
if ($state -notin @('absent','directory')) { throw "Unsafe existing artwork path" }
if ($state -eq 'directory') {
    $remoteManifest = ((& $Adb -s $Endpoint shell "cat '$remoteAssets/manifest.ini'") -join "`n") -replace "`r", ""
    $remotePerkMatch = [regex]::Match($remoteManifest, '(?m)^PerkCount=([0-9]+)$')
    if ($LASTEXITCODE -ne 0 -or
        $remoteManifest -notmatch '(?m)^Format=Fallout4SecondScreenAssetsV1$' -or
        $remoteManifest -notmatch '(?m)^InterfaceBa2Sha256=[0-9a-f]{64}$' -or
        $remoteManifest -notmatch '(?m)^SpecialCount=7$' -or
        $remoteManifest -notmatch '(?m)^ConditionBodyCount=17$' -or
        $remoteManifest -notmatch '(?m)^ConditionHeadCount=32$' -or
        -not $remotePerkMatch.Success -or
        [int]$remotePerkMatch.Groups[1].Value -lt 80) {
        throw "Existing artwork cache does not have a valid owned manifest"
    }
    $remotePerkCount = [int]$remotePerkMatch.Groups[1].Value
    $remoteHashLines = @(& $Adb -s $Endpoint shell "cat '$remoteAssets/files.sha256'")
    if ($LASTEXITCODE -ne 0) { throw "Could not read existing artwork ownership hashes" }
    $seenPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $remoteSpecialCount = 0
    $remoteBodyCount = 0
    $remoteHeadCount = 0
    $remotePerkPathCount = 0
    $remoteManifestCount = 0
    foreach ($line in $remoteHashLines) {
        $line = $line.TrimEnd("`r")
        if ($line -notmatch '^[0-9a-f]{64}  (special/[A-Za-z0-9_.-]+\.rgba|condition/body_[A-Za-z0-9_.-]+\.rgba|condition/head_[A-Za-z0-9_.-]+\.rgba|perks/[A-Za-z0-9_.-]+\.rgba|manifest\.ini)$') {
            throw "Existing artwork cache has an unsafe or duplicate ownership entry"
        }
        $ownedPath = $Matches[1]
        if (-not $seenPaths.Add($ownedPath)) {
            throw "Existing artwork cache has an unsafe or duplicate ownership entry"
        }
        switch -Regex ($ownedPath) {
            '^special/'       { $remoteSpecialCount++; break }
            '^condition/body_' { $remoteBodyCount++; break }
            '^condition/head_' { $remoteHeadCount++; break }
            '^perks/'          { $remotePerkPathCount++; break }
            '^manifest\.ini$' { $remoteManifestCount++; break }
        }
    }
    if ($remoteSpecialCount -ne 7 -or $remoteBodyCount -ne 17 -or
        $remoteHeadCount -ne 32 -or $remotePerkPathCount -ne $remotePerkCount -or
        $remoteManifestCount -ne 1) {
        throw "Existing artwork ownership list does not match its manifest"
    }
    $existingCount = ((& $Adb -s $Endpoint shell "find '$remoteAssets' -type f | wc -l") -join "`n").Trim()
    if ($LASTEXITCODE -ne 0 -or [int]$existingCount -ne ($seenPaths.Count + 1)) {
        throw "Existing artwork cache contains unowned files"
    }
    $existingLinks = ((& $Adb -s $Endpoint shell "find '$remoteAssets' -type l | wc -l") -join "`n").Trim()
    if ($LASTEXITCODE -ne 0 -or [int]$existingLinks -ne 0) {
        throw "Existing artwork cache contains symbolic links"
    }
    & $Adb -s $Endpoint shell "cd '$remoteAssets' && test -f files.sha256 && sha256sum -cs files.sha256"
    if ($LASTEXITCODE -ne 0) { throw "Existing artwork cache is not owned or was modified" }
}

Write-Host "Artwork deployment preflight passed"
Write-Host "  SPECIAL:       $($special.Count)"
Write-Host "  Condition body: $($bodies.Count)"
Write-Host "  Condition head: $($heads.Count)"
Write-Host "  Perks:          $($perks.Count)"
Write-Host "  Hash manifest:  $hashListHash"
if (-not $Commit) {
    Write-Host "Dry run only. Re-run with -Commit after reviewing this preflight."
    return
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmssfff'
$stage = "$RemoteGame/.Fallout4SecondScreen-assets-new-$stamp"
$backup = "$RemoteGame/.Fallout4SecondScreen-assets-backup-$stamp"
$published = $false
$movedExisting = $false
try {
    & $Adb -s $Endpoint shell "test ! -e '$stage' && mkdir '$stage'"
    if ($LASTEXITCODE -ne 0) { throw "Could not create the artwork staging directory" }
    foreach ($name in @('special','condition','perks')) {
        & $Adb -s $Endpoint push (Join-Path $assetRoot $name) "$stage/$name" | Write-Host
        if ($LASTEXITCODE -ne 0) { throw "Could not stage $name artwork" }
    }
    & $Adb -s $Endpoint push $manifestPath "$stage/manifest.ini" | Write-Host
    if ($LASTEXITCODE -ne 0) { throw "Could not stage the artwork manifest" }
    & $Adb -s $Endpoint push $hashListPath "$stage/files.sha256" | Write-Host
    if ($LASTEXITCODE -ne 0) { throw "Could not stage artwork hashes" }
    $remoteCount = ((& $Adb -s $Endpoint shell "find '$stage' -type f | wc -l") -join "`n").Trim()
    if ($LASTEXITCODE -ne 0 -or [int]$remoteCount -ne $expectedRemoteFiles) {
        throw "Staged artwork file count is incorrect"
    }
    $remoteLinks = ((& $Adb -s $Endpoint shell "find '$stage' -type l | wc -l") -join "`n").Trim()
    if ($LASTEXITCODE -ne 0 -or [int]$remoteLinks -ne 0) {
        throw "Staged artwork contains symbolic links"
    }
    & $Adb -s $Endpoint shell "cd '$stage' && sha256sum -cs files.sha256"
    if ($LASTEXITCODE -ne 0) { throw "Staged artwork failed SHA-256 verification" }
    if ($state -eq 'directory') {
        & $Adb -s $Endpoint shell "test ! -e '$backup' && mv '$remoteAssets' '$backup'"
        if ($LASTEXITCODE -ne 0) { throw "Could not back up existing artwork" }
        $movedExisting = $true
    }
    & $Adb -s $Endpoint shell "mv '$stage' '$remoteAssets'"
    if ($LASTEXITCODE -ne 0) { throw "Could not publish artwork" }
    $published = $true
    & $Adb -s $Endpoint shell "cd '$remoteAssets' && sha256sum -cs files.sha256"
    if ($LASTEXITCODE -ne 0) { throw "Published artwork failed verification" }
    Write-Host "Installed the user-owned Fallout 4 UI artwork cache." -ForegroundColor Green
    if ($movedExisting) { Write-Host "Backup: $backup" }
}
catch {
    $original = $_
    $rollbackProblems = [Collections.Generic.List[string]]::new()
    if ($published) {
        & $Adb -s $Endpoint shell "test '$remoteAssets' = '$RemoteGame/Fallout4SecondScreen.assets' && rm -rf '$remoteAssets'"
        if ($LASTEXITCODE -ne 0) { $rollbackProblems.Add('could not remove failed published cache') }
    } else {
        & $Adb -s $Endpoint shell "test '$stage' = '$RemoteGame/.Fallout4SecondScreen-assets-new-$stamp' && rm -rf '$stage'"
        if ($LASTEXITCODE -ne 0) { $rollbackProblems.Add('could not remove staging cache') }
    }
    if ($movedExisting) {
        & $Adb -s $Endpoint shell "test -d '$backup' && mv '$backup' '$remoteAssets'"
        if ($LASTEXITCODE -ne 0) {
            $rollbackProblems.Add('could not restore previous cache')
        } else {
            & $Adb -s $Endpoint shell "cd '$remoteAssets' && sha256sum -cs files.sha256"
            if ($LASTEXITCODE -ne 0) {
                $rollbackProblems.Add('restored cache failed verification')
            }
        }
    }
    if ($rollbackProblems.Count -ne 0) {
        throw "Artwork deployment failed and rollback was not verified ($($rollbackProblems -join '; ')). Original error: $original"
    }
    throw $original
}

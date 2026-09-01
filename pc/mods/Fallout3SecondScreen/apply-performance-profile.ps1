param(
    [Parameter(Mandatory = $true)]
    [string]$Endpoint,
    [switch]$Commit
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$adb = 'C:\Users\Shadow\AppData\Local\Android\Sdk\platform-tools\adb.exe'
$packageName = 'com.wemu'
$gameRoot = '/storage/emulated/0/Download/Fallout-3-GOTY-SteamRIP.com/Fallout 3'
$prefixRoot = '/data/user/0/com.wemu/files/imagefs/home/xuser-2/.wine'
$documents = "$prefixRoot/drive_c/users/xuser/Documents/My Games/Fallout3"
$falloutIni = "$documents/FALLOUT.INI"
$prefsIni = "$documents/FalloutPrefs.ini"
$dxvk = "$gameRoot/dxvk.conf"
$mainProfile = Join-Path $projectRoot 'Fallout3Performance.ini'
$prefsProfile = Join-Path $projectRoot 'Fallout3PrefsPerformance.ini'
$dxvkProfile = Join-Path $projectRoot 'dxvk.conf'
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$work = Join-Path ([IO.Path]::GetTempPath()) "fo3ds-performance-$stamp"
. (Join-Path $projectRoot 'tools\Ini.ps1')

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $priorPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $adb '-s' $Endpoint @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $priorPreference
    }
    if ($exitCode -ne 0) {
        throw "ADB failed: adb -s $Endpoint $($Arguments -join ' ')`n$($output -join "`n")"
    }
    return $output
}

function Quote-Remote([string]$Value) {
    $singleQuote = [string][char]39
    $escapedQuote = $singleQuote + '"' + $singleQuote + '"' + $singleQuote
    return $singleQuote + $Value.Replace($singleQuote, $escapedQuote) + $singleQuote
}

function Quote-RunAsPath([string]$Value) {
    # Invoke-RunAs already single-quotes the complete sh script. Arguments
    # inside that script therefore use double quotes; nesting Quote-Remote here
    # would be quoted a second time by adb and split a path at "My Games".
    return '"' + $Value.Replace('\\', '\\\\').Replace('"', '\"').Replace('$', '\$') + '"'
}

function Invoke-Remote([string]$Command) {
    return Invoke-Adb 'shell' $Command
}

function Invoke-RunAs([string]$Command) {
    return Invoke-Adb 'shell' 'run-as' $packageName 'sh' '-c' (Quote-Remote $Command)
}

function Read-PrivateFile([string]$PrivatePath, [string]$LocalPath) {
    $shared = "/storage/emulated/0/Download/.fo3ds-perf-$stamp-$([IO.Path]::GetFileName($PrivatePath))"
    Invoke-Adb 'shell' 'run-as' $packageName 'cp' (Quote-Remote $PrivatePath) (Quote-Remote $shared) | Out-Null
    try {
        Invoke-Adb 'pull' $shared $LocalPath | Out-Null
    } finally {
        Invoke-Remote ("rm -f " + (Quote-Remote $shared)) | Out-Null
    }
}

function Install-PrivateFile([string]$LocalPath, [string]$PrivatePath,
                             [string]$Tag) {
    $shared = "/storage/emulated/0/Download/.fo3ds-perf-$stamp-$Tag"
    Invoke-Adb 'push' $LocalPath $shared | Out-Null
    try {
        Invoke-Adb 'shell' 'run-as' $packageName 'cp' (Quote-Remote $shared) `
                   (Quote-Remote "$PrivatePath.fo3ds-new") | Out-Null
        Invoke-Adb 'shell' 'run-as' $packageName 'mv' `
                   (Quote-Remote "$PrivatePath.fo3ds-new") `
                   (Quote-Remote $PrivatePath) | Out-Null
    } finally {
        Invoke-Remote ("rm -f " + (Quote-Remote $shared)) | Out-Null
    }
}

foreach ($required in @($adb, $mainProfile, $prefsProfile, $dxvkProfile)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required file missing: $required" }
}
New-Item -ItemType Directory -Path $work | Out-Null

try {
    & $adb connect $Endpoint | Out-Null
    if ($LASTEXITCODE -ne 0 -or (Invoke-Adb 'get-state' | Select-Object -Last 1).Trim() -ne 'device') {
        throw 'ADB endpoint is not ready.'
    }
    $processes = (Invoke-Remote 'ps -A') -join "`n"
    if ($processes -match '(?i)Fallout3|wineserver|winedevice|wine-preloader|wine64-preloader') {
        throw 'Fallout/Wine is active. Exit the game and stop the container before changing its live INI files.'
    }

    $mainOriginal = Join-Path $work 'FALLOUT.INI.original'
    $prefsOriginal = Join-Path $work 'FalloutPrefs.ini.original'
    $dxvkOriginal = Join-Path $work 'dxvk.conf.original'
    $mainPatched = Join-Path $work 'FALLOUT.INI.patched'
    $prefsPatched = Join-Path $work 'FalloutPrefs.ini.patched'
    $dxvkPatched = Join-Path $work 'dxvk.conf.patched'
    Read-PrivateFile $falloutIni $mainOriginal
    Read-PrivateFile $prefsIni $prefsOriginal
    Invoke-Adb 'pull' $dxvk $dxvkOriginal | Out-Null
    Merge-IniProfile $mainOriginal $mainProfile $mainPatched
    Merge-IniProfile $prefsOriginal $prefsProfile $prefsPatched
    Merge-IniProfile $dxvkOriginal $dxvkProfile $dxvkPatched

    Write-Host 'Fallout 3 performance profile preflight passed.'
    Write-Host "  active main INI:  $falloutIni"
    Write-Host "  active prefs INI: $prefsIni"
    Write-Host "  DXVK config:      $dxvk"
    Write-Host '  resolution, controls, gameplay, and save settings are preserved'
    if (-not $Commit) {
        Write-Host 'Dry run only. Re-run with -Commit to apply the backed-up profile.'
        return
    }

    $gameBackup = "$gameRoot/.fallout3-performance-backups/$stamp"
    $privateBackup = "$prefixRoot/.fallout3-performance-backups/$stamp"
    $commitStarted = $false
    try {
        Invoke-Remote ("mkdir -p " + (Quote-Remote $gameBackup) + ' && cp ' +
                       (Quote-Remote $dxvk) + ' ' +
                       (Quote-Remote "$gameBackup/dxvk.conf")) | Out-Null
        Invoke-Adb 'shell' 'run-as' $packageName 'mkdir' '-p' `
                   (Quote-Remote $privateBackup) | Out-Null
        Invoke-Adb 'shell' 'run-as' $packageName 'cp' `
                   (Quote-Remote $falloutIni) `
                   (Quote-Remote "$privateBackup/FALLOUT.INI") | Out-Null
        Invoke-Adb 'shell' 'run-as' $packageName 'cp' `
                   (Quote-Remote $prefsIni) `
                   (Quote-Remote "$privateBackup/FalloutPrefs.ini") | Out-Null
        $commitStarted = $true

        $processes = (Invoke-Remote 'ps -A') -join "`n"
        if ($processes -match '(?i)Fallout3|wineserver|winedevice|wine-preloader|wine64-preloader') {
            throw 'Fallout/Wine started during profile staging; no live configuration was changed.'
        }
        Install-PrivateFile $mainPatched $falloutIni 'FALLOUT.INI'
        Install-PrivateFile $prefsPatched $prefsIni 'FalloutPrefs.ini'
        $dxvkTemp = "$gameRoot/.dxvk.conf.fo3ds-$stamp.tmp"
        Invoke-Adb 'push' $dxvkPatched $dxvkTemp | Out-Null
        Invoke-Remote ("mv " + (Quote-Remote $dxvkTemp) + ' ' + (Quote-Remote $dxvk)) | Out-Null

        $mainVerify = Join-Path $work 'FALLOUT.INI.verify'
        $prefsVerify = Join-Path $work 'FalloutPrefs.ini.verify'
        $dxvkVerify = Join-Path $work 'dxvk.conf.verify'
        Read-PrivateFile $falloutIni $mainVerify
        Read-PrivateFile $prefsIni $prefsVerify
        Invoke-Adb 'pull' $dxvk $dxvkVerify | Out-Null
        foreach ($pair in @(@($mainPatched, $mainVerify), @($prefsPatched, $prefsVerify),
                            @($dxvkPatched, $dxvkVerify))) {
            $expected = (Get-FileHash -Algorithm SHA256 -LiteralPath $pair[0]).Hash
            $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $pair[1]).Hash
            if ($expected -ne $actual) { throw "Post-install profile verification failed: $($pair[1])" }
        }
        Write-Host "Performance profile committed and verified. Backup: $gameBackup / $privateBackup"
    } catch {
        $commitError = $_
        if ($commitStarted) {
            Write-Warning 'Profile commit failed; restoring all three backed-up files.'
            $rollbackErrors = @()
            try {
                Invoke-Remote ("cp " + (Quote-Remote "$gameBackup/dxvk.conf") +
                               ' ' + (Quote-Remote $dxvk)) | Out-Null
            } catch { $rollbackErrors += "dxvk: $($_.Exception.Message)" }
            try {
                Invoke-Adb 'shell' 'run-as' $packageName 'cp' `
                           (Quote-Remote "$privateBackup/FALLOUT.INI") `
                           (Quote-Remote $falloutIni) | Out-Null
                Invoke-Adb 'shell' 'run-as' $packageName 'cp' `
                           (Quote-Remote "$privateBackup/FalloutPrefs.ini") `
                           (Quote-Remote $prefsIni) | Out-Null
            } catch { $rollbackErrors += "INI: $($_.Exception.Message)" }
            if ($rollbackErrors.Count) {
                Write-Warning ("Rollback was incomplete: " + ($rollbackErrors -join ' | '))
            } else { Write-Warning 'Rollback completed.' }
        }
        throw $commitError
    }
} finally {
    if (Test-Path -LiteralPath $work) {
        $resolvedWork = [IO.Path]::GetFullPath($work)
        $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if (-not $resolvedWork.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetFileName($resolvedWork) -notlike 'fo3ds-performance-*') {
            throw "Refusing to remove unexpected work directory: $resolvedWork"
        }
        Remove-Item -LiteralPath $resolvedWork -Recurse -Force
    }
}

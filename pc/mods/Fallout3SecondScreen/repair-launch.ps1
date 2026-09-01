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
$gameExe = "$gameRoot/Fallout3.exe"
$sourcePrefs = "$gameRoot/Fallout3/FalloutPrefs.ini"
$prefixRoot = '/data/user/0/com.wemu/files/imagefs/home/xuser-2/.wine'
$systemReg = "$prefixRoot/system.reg"
$prefsDirectory = "$prefixRoot/drive_c/users/xuser/Documents/My Games/Fallout3"
$prefsTarget = "$prefsDirectory/FalloutPrefs.ini"
# `adb shell run-as ... sh -c` rebuilds its command line twice. Embedded POSIX
# quotes do not survive both parsers reliably on this Android build, while a
# backslash-escaped space does (and these fixed paths contain no other shell
# metacharacters requiring expansion).
$sourcePrefsShell = $sourcePrefs.Replace(' ', '\ ')
$prefsDirectoryShell = $prefsDirectory.Replace(' ', '\ ')
$prefsTargetShell = $prefsTarget.Replace(' ', '\ ')
$targetExeHash = 'c3f97c2255fa041a851c17cf372d69aaadd8694e2dc4230ba556001bbfbd2f3e'
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$localWork = Join-Path $env:TEMP ("fo3ds-launch-repair-" + $stamp)
. (Join-Path $projectRoot 'tools\Registry.ps1')

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

function Invoke-Remote([string]$Command) {
    return Invoke-Adb 'shell' $Command
}

function Invoke-RunAs([string]$Command) {
    return Invoke-Adb 'shell' 'run-as' $packageName 'sh' '-c' (Quote-Remote $Command)
}

function Read-PrivateFile([string]$PrivatePath, [string]$LocalPath) {
    $shared = "/storage/emulated/0/Download/.fo3ds-$stamp-$([IO.Path]::GetFileName($PrivatePath))"
    Invoke-RunAs ("cp " + (Quote-Remote $PrivatePath) + ' ' +
                  (Quote-Remote $shared)) | Out-Null
    try {
        Invoke-Adb 'pull' $shared $LocalPath | Out-Null
    } finally {
        try { Invoke-Remote ("rm -f " + (Quote-Remote $shared)) | Out-Null }
        catch { Write-Warning "Could not remove temporary read file: $($_.Exception.Message)" }
    }
}

New-Item -ItemType Directory -Force -Path $localWork | Out-Null
try {
    & $adb 'connect' $Endpoint | Out-Null
    if ($LASTEXITCODE -ne 0 -or
        (Invoke-Adb 'get-state' | Select-Object -Last 1).Trim() -ne 'device') {
        throw 'ADB endpoint is not ready.'
    }
    $processes = (Invoke-Remote 'ps -A') -join "`n"
    if ($processes -match '(?i)Fallout3|wineserver|wine-preloader|wine64-preloader|winedevice') {
        throw 'Fallout/Wine is active. Fully exit the WEMU container before repair.'
    }
    $exeHash = ((Invoke-Remote ("sha256sum " + (Quote-Remote $gameExe)) |
                 Select-Object -Last 1) -split '\s+')[0].ToLowerInvariant()
    if ($exeHash -ne $targetExeHash) { throw "Unsupported Fallout3.exe: $exeHash" }
    $prefsSourceHash = ((Invoke-Remote ("sha256sum " + (Quote-Remote $sourcePrefs)) |
                         Select-Object -Last 1) -split '\s+')[0].ToLowerInvariant()

    $systemOriginal = Join-Path $localWork 'system.reg.original'
    $systemPatched = Join-Path $localWork 'system.reg.patched'
    Read-PrivateFile $systemReg $systemOriginal
    $priorInstalledPath = Set-FalloutInstalledPath $systemOriginal $systemPatched
    $priorPrefs = (Invoke-RunAs ("if [ -f " + $prefsTargetShell +
                  ' ]; then sha256sum ' + $prefsTargetShell +
                  '; else echo absent; fi') | Select-Object -Last 1)

    Write-Host 'Fallout 3 launch-repair preflight passed.'
    Write-Host "  executable:       $exeHash"
    Write-Host "  Installed Path:   $(if ($priorInstalledPath) { $priorInstalledPath } else { 'absent' })"
    Write-Host "  preferences:      $priorPrefs"
    Write-Host "  supplied prefs:   $prefsSourceHash"
    if (-not $Commit) {
        Write-Host 'Dry run only. Re-run with -Commit after reviewing this output.'
        return
    }

    $backup = "$prefixRoot/.fallout3-dualscreen-backups/$stamp/launch-repair"
    $registryShared = "/storage/emulated/0/Download/.fo3ds-$stamp-system.reg"
    $commitStarted = $false
    try {
        Invoke-RunAs ("mkdir -p " + (Quote-Remote $backup)) | Out-Null
        Invoke-RunAs ("cp " + (Quote-Remote $systemReg) + ' ' +
                      (Quote-Remote "$backup/system.reg")) | Out-Null
        if ($priorPrefs -ne 'absent') {
            Invoke-RunAs ("cp " + $prefsTargetShell + ' ' +
                          (Quote-Remote "$backup/FalloutPrefs.ini")) | Out-Null
        }
        $commitStarted = $true
        Invoke-Adb 'push' $systemPatched $registryShared | Out-Null
        Invoke-RunAs ("cp " + (Quote-Remote $registryShared) + ' ' +
                      (Quote-Remote "$systemReg.fo3ds-new") + ' && mv ' +
                      (Quote-Remote "$systemReg.fo3ds-new") + ' ' +
                      (Quote-Remote $systemReg)) | Out-Null
        if ($priorPrefs -eq 'absent') {
            Invoke-RunAs ("mkdir -p " + $prefsDirectoryShell +
                          ' && cp ' + $sourcePrefsShell + ' ' +
                          "$prefsTargetShell.fo3ds-new" + ' && mv ' +
                          "$prefsTargetShell.fo3ds-new" + ' ' +
                          $prefsTargetShell) | Out-Null
        } else {
            # FalloutLauncherSteam writes the adapter name it actually observed
            # through the active DXVK/Wine stack. Replacing that live file with
            # the archive's generic 960x600 template reintroduces the renderer
            # validation failure this repair is meant to remove.
            Write-Host '  preserving active launcher-generated FalloutPrefs.ini'
        }

        $verifySystem = Join-Path $localWork 'system.reg.verify'
        Read-PrivateFile $systemReg $verifySystem
        $verifyText = Get-Content -Raw -LiteralPath $verifySystem
        if ($verifyText -notmatch '(?s)\[Software\\\\Wow6432Node\\\\Bethesda Softworks\\\\Fallout3\][^\[]*"Installed Path"="F:\\\\Download\\\\Fallout-3-GOTY-SteamRIP\.com\\\\Fallout 3\\\\"') {
            throw 'Installed Path verification failed.'
        }
        $prefsInstalledHash = ((Invoke-RunAs ("sha256sum " +
                               $prefsTargetShell) |
                               Select-Object -Last 1) -split '\s+')[0].ToLowerInvariant()
        $expectedPrefsHash = if ($priorPrefs -eq 'absent') {
            $prefsSourceHash
        } else {
            (($priorPrefs -split '\s+')[0]).ToLowerInvariant()
        }
        if ($prefsInstalledHash -ne $expectedPrefsHash) {
            throw "FalloutPrefs.ini verification failed: $prefsInstalledHash"
        }
        Write-Host "Launch repair committed and verified. Backup: $backup"
    } catch {
        $commitError = $_
        if ($commitStarted) {
            Write-Warning 'Launch repair failed after commit began; restoring prefix backup.'
            Invoke-RunAs ("cp " + (Quote-Remote "$backup/system.reg") + ' ' +
                          (Quote-Remote $systemReg)) | Out-Null
            if ($priorPrefs -ne 'absent') {
                Invoke-RunAs ("cp " + (Quote-Remote "$backup/FalloutPrefs.ini") +
                              ' ' + $prefsTargetShell) | Out-Null
            } else {
                Invoke-RunAs ("rm -f " + $prefsTargetShell) | Out-Null
            }
            Write-Warning 'Launch repair rollback completed.'
        }
        throw $commitError
    } finally {
        try { Invoke-Remote ("rm -f " + (Quote-Remote $registryShared)) | Out-Null }
        catch { Write-Warning "Remote cleanup could not complete: $($_.Exception.Message)" }
    }
} finally {
    if (Test-Path -LiteralPath $localWork) {
        Remove-Item -LiteralPath $localWork -Recurse -Force
    }
}

param(
    [Parameter(Mandatory = $true)]
    [string]$Endpoint,
    [string]$GameRoot = '/storage/emulated/0/Download/Fallout-3-GOTY-SteamRIP.com/Fallout 3',
    [switch]$Commit
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$adb = 'C:\Users\Shadow\AppData\Local\Android\Sdk\platform-tools\adb.exe'
$packageRoot = Join-Path $projectRoot 'package'
$proxy = Join-Path $packageRoot 'dinput8.dll'
$ini = Join-Path $packageRoot 'Fallout3SecondScreen.ini'
$manifestPath = Join-Path $packageRoot 'manifest.json'
$gameRoot = $GameRoot.TrimEnd('/')
$gameExe = "$gameRoot/Fallout3.exe"
$prefixRoot = '/data/user/0/com.wemu/files/imagefs/home/xuser-2/.wine'
$userReg = "$prefixRoot/user.reg"
$containerFile = '/data/user/0/com.wemu/files/imagefs/home/xuser-2/.container'
$targetExeHash = 'c3f97c2255fa041a851c17cf372d69aaadd8694e2dc4230ba556001bbfbd2f3e'
$targetExeSize = 16855040
$packageName = 'com.wemu'
$knownPreviousProxyHashes = @(
    # Verified launcher payload immediately preceding the native CND canvas,
    # persistent map-view, discovered-marker and radio synchronization fixes.
    '1b0ca9fc3d85ab13aca7c61d6409209e96010e568ffcec3acc7410785486e74a',
    # Verified local release immediately preceding native quest-target pins
    # and the final shared map viewport correction.
    'b4bbdd57eedafea7708b32c53730c66121d9c554ffb81ed2fb65d1b1679df7d2',
    # Verified launcher payload immediately preceding the native map-coordinate
    # and canonical Stats artwork correction.
    '675786af5592dcd1d94338dadf8850bbbc91249f45c4e23cfed302ba4252c60e',
    # Verified condition-figure/panel-lifecycle build immediately preceding
    # the native-reference DATA/STATS overhaul.
    '2e40303f21f22c1c1fd4cbde79e0a44d45e3346296b176ebca7037b2abf0b2a0',
    # Verified payload embedded by the immediately preceding DualScreen
    # Launcher APK. This is the live predecessor being upgraded by the
    # condition-figure and panel-lifecycle release.
    '35d0d2ba69c1f60ee1332b24a988fd4edfc0ae5c13d9a303f5cc6b6e77fc972b',
    # Verified live hostile-health diagnostic build immediately before the
    # native Status hierarchy correction and bounded wait-call tracer.
    'c42e446fc38bbadce7d629eef360a54bcc1fe14de5ef0067c5603a3685a2afd3',
    # Verified on-device localized journal/icon build immediately before the
    # inventory-frame correction and live combat-health call-site diagnostics.
    '1e5f41efd2fdb4095e6bde2e43cb0a72dc0a9611fd9ef68399918416c89261f3',
    # Verified on-device page-overhaul build immediately before wiring the
    # native localized quest-journal and note-text resolvers.
    'b83a8994f1ab161c257f537a2819aba70e4185313b0917f8168b22be8b6a27b1',
    # Verified on-device build immediately before the real record-icon,
    # detail-scrolling, and cross-page interaction audit.
    '6ff8f9a498659241365d2a67ffad35bb0ec690d3c18fb7c28c223ff14c42a38d',
    # Verified device build immediately before caching the immutable CRT
    # background and retaining live combat-stall instrumentation.
    '73a74ffe80acff4235b77a6965de89078f4382b6c7966e30ce6ea0b392170a61',
    # Verified on-device build immediately before this full safety/state audit.
    '640deca8207f77678503d94f8afc62b5bd080bfb7ce060400235e4b4fe3362d2',
    # Verified on-device build immediately before the vanilla STATS artwork
    # and exact combat-health timing fix.
    'f35fd1bd1af395ed3b4279ea013ee5dfa34df0df53480707f096e372b2210036',
    # Verified performance-instrumented build installed immediately before
    # correcting the data-scan retry storm and ActorValue base-value ABI.
    '2ff7c6e71cd5ec8647e0caaf3dac8c4d1d41c0d6861d053b9618f85f9f27757b',
    # Verified cross-page XP/effects/map-marker build installed immediately
    # before simplifying the initialization presentation.
    '49c414ddca9847d92694906bb72ee4c537b859ea945d19196ef2b30767379c7b',
    # Verified live-radio build currently installed before the cross-page
    # XP/effects/map-marker and lifecycle synchronization audit.
    '873b73373ba9893e31259673bfc9d7e2583b070781f50215c1924088ba3e9473',
    # Verified action-audited build installed immediately before the live
    # radio page and radio performance-gating implementation.
    'b0885460d10435e9993cc5ca24b52856709143f62b38879ac3cad18927f4f15e',
    # Verified full page-wiring build installed immediately before the final
    # controller-selection and pending-action audit.
    'fc1375f44509b955e83d0cc6767ad073eeab0f49a97800919d9bf8bdd0c4db59',
    # Verified on-device FO3DS build from the immediately preceding deployment.
    # It has the expected PE32 dinput8 proxy exports and FO3DS build identity.
    '3db4eee5c5eaa972e91fa03d4ebe1bad904e88c29ed6f1344621629d12b5aa9b',
    # Verified build installed immediately before the Pip-Boy UI redesign.
    '8445c385a287d1d8ded46e7351fdf9b32f793ab2824baf176270cdb834a1ecd3',
    # Verified byte-for-byte against Companion's previously staged FO3 mod.
    'e1fe1f02bc99afcdc70ddf23ee746bf1cf7e98003d28a9dea6800fe0c1c21367'
)
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$localWork = Join-Path $env:TEMP ("fo3ds-deploy-" + $stamp)
. (Join-Path $projectRoot 'tools\Registry.ps1')

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    # adb writes normal transfer progress to stderr. Under the script-wide
    # Stop policy PowerShell would otherwise turn a successful pull/push into
    # a terminating NativeCommandError before its exit code can be checked.
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
    # adb shell rebuilds its remote command line from argv. Quote the complete
    # script so sh -c receives it as argv[0], rather than receiving only the
    # first word (for example, a bare `cp` with no operands).
    return Invoke-Adb 'shell' 'run-as' $packageName 'sh' '-c' (Quote-Remote $Command)
}

function Require-LocalPayload {
    if (-not (Test-Path -LiteralPath $proxy) -or
        -not (Test-Path -LiteralPath $ini) -or
        -not (Test-Path -LiteralPath $manifestPath)) {
        throw 'Release package is missing. Run build.ps1 -Configuration Release first.'
    }
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    $payloadHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $proxy).Hash.ToLowerInvariant()
    if ([string]$manifest.configuration -ne 'Release' -or
        [string]$manifest.sha256 -ne $payloadHash -or
        [int64]$manifest.size -ne (Get-Item -LiteralPath $proxy).Length) {
        throw 'Release package manifest does not match dinput8.dll. Rebuild Release.'
    }
    $imports = & 'C:\msys64\mingw32\bin\objdump.exe' '-p' $proxy
    if ($LASTEXITCODE -ne 0 -or ($imports -join "`n") -notmatch 'pei-i386') {
        throw 'Packaged proxy is not a PE32 DLL.'
    }
    foreach ($name in @('DirectInput8Create', 'DllCanUnloadNow', 'DllGetClassObject',
                        'DllRegisterServer', 'DllUnregisterServer')) {
        if (($imports -join "`n") -notmatch [regex]::Escape($name)) {
            throw "Packaged proxy is missing export $name."
        }
    }
}

function Read-PrivateFile([string]$PrivatePath, [string]$LocalPath) {
    $shared = "/storage/emulated/0/Download/.fo3ds-$stamp-$([IO.Path]::GetFileName($PrivatePath))"
    Invoke-RunAs ("cp " + (Quote-Remote $PrivatePath) + ' ' + (Quote-Remote $shared)) | Out-Null
    try {
        Invoke-Adb 'pull' $shared $LocalPath | Out-Null
    } finally {
        Invoke-Remote ("rm -f " + (Quote-Remote $shared)) | Out-Null
    }
}

Require-LocalPayload
if (-not (Test-Path -LiteralPath $adb)) { throw "ADB not found: $adb" }
New-Item -ItemType Directory -Force -Path $localWork | Out-Null

try {
    $connectOutput = & $adb 'connect' $Endpoint 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "ADB connect failed: $($connectOutput -join "`n")"
    }
    $state = (Invoke-Adb 'get-state' | Select-Object -Last 1).Trim()
    if ($state -ne 'device') { throw "ADB endpoint is not ready: $state" }

    $exeSize = [int64]((Invoke-Remote ("stat -c %s " + (Quote-Remote $gameExe)) |
                        Select-Object -Last 1).Trim())
    $exeHash = ((Invoke-Remote ("sha256sum " + (Quote-Remote $gameExe)) |
                 Select-Object -Last 1) -split '\s+')[0].ToLowerInvariant()
    if ($exeSize -ne $targetExeSize -or $exeHash -ne $targetExeHash) {
        throw "Unsupported Fallout3.exe size/hash: $exeSize / $exeHash"
    }

    $processes = (Invoke-Remote 'ps -A') -join "`n"
    if ($processes -match '(?i)Fallout3|wineserver|winedevice|wine-preloader|wine64-preloader') {
        throw 'Fallout/Wine is active. Exit the game and stop the container before deployment.'
    }

    $containerLocal = Join-Path $localWork '.container'
    Read-PrivateFile $containerFile $containerLocal
    $container = Get-Content -Raw -LiteralPath $containerLocal | ConvertFrom-Json
    $secondScreen = [string]$container.extraData.displaySecondScreen
    $ownsDesktop = [string]$container.extraData.wemuOwnsDesktop
    if ($secondScreen -ne '1') {
        throw 'Games container does not currently enable displaySecondScreen=1.'
    }
    if ($ownsDesktop -ne '1') {
        throw 'Games container does not currently enable wemuOwnsDesktop=1.'
    }

    $existingProxy = "$gameRoot/dinput8.dll"
    $existingProxyHash = $null
    $existingProbe = (Invoke-Remote ("if [ -f " + (Quote-Remote $existingProxy) +
                     ' ]; then sha256sum ' + (Quote-Remote $existingProxy) +
                     '; else echo absent; fi') | Select-Object -Last 1)
    if ($existingProbe -ne 'absent') {
        $existingProxyHash = ($existingProbe -split '\s+')[0].ToLowerInvariant()
        $newHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $proxy).Hash.ToLowerInvariant()
        if ($existingProxyHash -ne $newHash -and
            $knownPreviousProxyHashes -notcontains $existingProxyHash) {
            throw "An unclassified game-local dinput8.dll already exists ($existingProxyHash). Refusing to overwrite it."
        }
    }
    $existingIni = "$gameRoot/Fallout3SecondScreen.ini"
    $existingIniPresent = ((Invoke-Remote ("if [ -f " +
                           (Quote-Remote $existingIni) +
                           ' ]; then echo present; else echo absent; fi') |
                           Select-Object -Last 1) -eq 'present')

    $registryOriginal = Join-Path $localWork 'user.reg.original'
    $registryPatched = Join-Path $localWork 'user.reg.patched'
    Read-PrivateFile $userReg $registryOriginal
    $PriorRegistryValue = Set-FalloutDinputOverride $registryOriginal $registryPatched

    Write-Host 'Fallout 3 dual-screen deployment preflight passed.'
    Write-Host "  endpoint:       $Endpoint"
    Write-Host "  executable:     $exeHash ($exeSize bytes)"
    Write-Host "  game directory: $gameRoot"
    Write-Host "  prefix:         $prefixRoot"
    Write-Host "  previous proxy: $(if ($existingProxyHash) { $existingProxyHash } else { 'absent' })"
    Write-Host "  registry prior: $(if ($PriorRegistryValue) { $PriorRegistryValue } else { 'value absent' })"
    if (-not $Commit) {
        Write-Host 'Dry run only. Re-run with -Commit after reviewing this output.'
        return
    }

    $gameBackup = "$gameRoot/.fallout3-dualscreen-backups/$stamp"
    $privateBackup = "$prefixRoot/.fallout3-dualscreen-backups/$stamp"
    $remoteProxyTemp = "$gameRoot/.dinput8.dll.fo3ds-$stamp.tmp"
    $remoteIniTemp = "$gameRoot/.Fallout3SecondScreen.ini.fo3ds-$stamp.tmp"
    $registryShared = "/storage/emulated/0/Download/.fo3ds-$stamp-user.reg.patched"
    $commitStarted = $false
    try {
        Invoke-Remote ("mkdir -p " + (Quote-Remote $gameBackup)) | Out-Null
        Invoke-RunAs ("mkdir -p " + (Quote-Remote $privateBackup)) | Out-Null
        if ($existingProxyHash) {
            Invoke-Remote ("cp " + (Quote-Remote $existingProxy) + ' ' +
                           (Quote-Remote "$gameBackup/dinput8.dll")) | Out-Null
        }
        if ($existingIniPresent) {
            Invoke-Remote ("cp " + (Quote-Remote $existingIni) + ' ' +
                           (Quote-Remote "$gameBackup/Fallout3SecondScreen.ini")) |
                           Out-Null
        }
        Invoke-RunAs ("cp " + (Quote-Remote $userReg) + ' ' +
                      (Quote-Remote "$privateBackup/user.reg")) | Out-Null
        $commitStarted = $true

        Invoke-Adb 'push' $proxy $remoteProxyTemp | Out-Null
        if (-not $existingIniPresent) {
            Invoke-Adb 'push' $ini $remoteIniTemp | Out-Null
        }

        # Recheck immediately before replacing a live-loadable DLL. This
        # prevents a container started during staging from observing a partial
        # deployment.
        $processes = (Invoke-Remote 'ps -A') -join "`n"
        if ($processes -match '(?i)Fallout3|wineserver|winedevice|wine-preloader|wine64-preloader') {
            throw 'Fallout/Wine started during deployment; staged files were not committed.'
        }
        Invoke-Remote ("mv " + (Quote-Remote $remoteProxyTemp) + ' ' +
                       (Quote-Remote $existingProxy)) | Out-Null
        if (-not $existingIniPresent) {
            Invoke-Remote ("mv " + (Quote-Remote $remoteIniTemp) + ' ' +
                           (Quote-Remote $existingIni)) | Out-Null
        }

        Invoke-Adb 'push' $registryPatched $registryShared | Out-Null
        Invoke-RunAs ("cp " + (Quote-Remote $registryShared) + ' ' +
                      (Quote-Remote "$userReg.fo3ds-new") + ' && mv ' +
                      (Quote-Remote "$userReg.fo3ds-new") + ' ' +
                      (Quote-Remote $userReg)) | Out-Null

        $installedHash = ((Invoke-Remote ("sha256sum " +
                          (Quote-Remote $existingProxy)) |
                          Select-Object -Last 1) -split '\s+')[0].ToLowerInvariant()
        $expectedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $proxy).Hash.ToLowerInvariant()
        if ($installedHash -ne $expectedHash) {
            throw "Post-install proxy verification failed: $installedHash"
        }
        $verifyReg = Join-Path $localWork 'user.reg.verify'
        Read-PrivateFile $userReg $verifyReg
        if ((Get-Content -Raw -LiteralPath $verifyReg) -notmatch
            '(?s)\[Software\\\\Wine\\\\AppDefaults\\\\Fallout3\.exe\\\\DllOverrides\][^\[]*"dinput8"="native,builtin"') {
            throw 'Post-install per-executable registry verification failed.'
        }
        Write-Host "Deployment committed and verified. Backup: $gameBackup / $privateBackup"
    } catch {
        $commitError = $_
        if ($commitStarted) {
            Write-Warning 'Deployment failed after commit began; restoring backed-up state.'
            $rollbackErrors = @()
            try {
                if ($existingProxyHash) {
                    Invoke-Remote ("cp " + (Quote-Remote "$gameBackup/dinput8.dll") +
                                   ' ' + (Quote-Remote $existingProxy)) | Out-Null
                } else {
                    Invoke-Remote ("rm -f " + (Quote-Remote $existingProxy)) | Out-Null
                }
            } catch {
                $rollbackErrors += "proxy: $($_.Exception.Message)"
            }
            try {
                if ($existingIniPresent) {
                    Invoke-Remote ("cp " +
                                   (Quote-Remote "$gameBackup/Fallout3SecondScreen.ini") +
                                   ' ' + (Quote-Remote $existingIni)) | Out-Null
                } else {
                    Invoke-Remote ("rm -f " + (Quote-Remote $existingIni)) | Out-Null
                }
            } catch {
                $rollbackErrors += "ini: $($_.Exception.Message)"
            }
            try {
                Invoke-RunAs ("cp " + (Quote-Remote "$privateBackup/user.reg") +
                              ' ' + (Quote-Remote $userReg)) | Out-Null
            } catch {
                $rollbackErrors += "registry: $($_.Exception.Message)"
            }
            if ($rollbackErrors.Count) {
                Write-Warning ("Rollback was incomplete: " +
                               ($rollbackErrors -join ' | '))
            } else {
                Write-Warning 'Rollback completed.'
            }
        }
        throw $commitError
    } finally {
        try {
            Invoke-Remote ("rm -f " + (Quote-Remote $remoteProxyTemp) + ' ' +
                           (Quote-Remote $remoteIniTemp) + ' ' +
                           (Quote-Remote $registryShared)) | Out-Null
        } catch {
            # Cleanup is best-effort. If ADB disconnects after a commit or
            # rollback failure, preserving the original exception is more
            # important than reporting failure to remove temporary files.
            Write-Warning ("Remote temporary-file cleanup could not complete: " +
                           $_.Exception.Message)
        }
    }
} finally {
    if (Test-Path -LiteralPath $localWork) {
        Remove-Item -LiteralPath $localWork -Recurse -Force
    }
}

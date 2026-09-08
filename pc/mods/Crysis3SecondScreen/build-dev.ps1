param([string]$Compiler='C:/msys64/mingw32/bin/i686-w64-mingw32-g++.exe')
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $MyInvocation.MyCommand.Path
$workspace=[IO.Path]::GetFullPath((Join-Path $project '../../..'))
$output=Join-Path $workspace '.work/crysis3/dev'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$previousPath=$env:PATH
try {
    $env:PATH=(Split-Path -Parent $Compiler)+';'+$previousPath
    $flags=@('-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-static','-static-libgcc','-static-libstdc++')
    foreach ($reader in @('NativeActorRead','NativeWeaponRead','NativeMissionRead','NativeObjectiveRead','NativeSuitRead','NativeSuitInputRead','NativeStatsRead','CommandInbox','ReadOnlyTrial','PublishTrialReplay','WeaponModel')) {
        $readerOutput=Join-Path $output ($reader+'-tests.exe')
        & $Compiler @flags (Join-Path $project ('tests/'+$reader+'Tests.cpp')) -o $readerOutput
        if ($LASTEXITCODE -ne 0) { throw "$reader compilation failed" }
        if ($reader -eq 'WeaponModel') { & $readerOutput (Join-Path $output 'model-test-fixtures') }
        elseif ($reader -in @('NativeSuitRead','NativeStatsRead','PublishTrialReplay')) { & $readerOutput $workspace }
        else { & $readerOutput }
        if ($LASTEXITCODE -ne 0) { throw "$reader tests failed" }
    }
    $touch=Join-Path $project '../DishonoredSecondScreen/src/Touch.cpp'
    $presentationTests=Join-Path $output 'panel-presentation-tests.exe'
    & $Compiler @flags (Join-Path $project '../DishonoredSecondScreen/tests/PanelPresentationTests.cpp') (Join-Path $project '../DeadIslandSecondScreen/src/Canvas.cpp') -lgdi32 -luser32 -o $presentationTests
    if ($LASTEXITCODE -ne 0) { throw 'Panel presentation compilation failed' }
    & $presentationTests
    if ($LASTEXITCODE -ne 0) { throw 'Panel presentation tests failed' }
    & $Compiler @flags (Join-Path $project 'tests/CoreTests.cpp') $touch -lws2_32 -o (Join-Path $output 'core-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Core compilation failed' }
    & (Join-Path $output 'core-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Core tests failed' }
    & $Compiler @flags (Join-Path $project 'tests/SuitCaptureTests.cpp') -o (Join-Path $output 'suit-capture-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Suit capture compilation failed' }
    & (Join-Path $output 'suit-capture-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Suit capture tests failed' }
    & $Compiler @flags (Join-Path $project 'tests/VitalsCaptureTests.cpp') -o (Join-Path $output 'vitals-capture-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Vitals capture compilation failed' }
    & (Join-Path $output 'vitals-capture-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Vitals capture tests failed' }
    & $Compiler @flags (Join-Path $project 'tests/AttachmentCaptureTests.cpp') -o (Join-Path $output 'attachment-capture-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Attachment capture compilation failed' }
    & (Join-Path $output 'attachment-capture-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Attachment capture tests failed' }
    & $Compiler @flags (Join-Path $project 'tests/MapCaptureTests.cpp') -o (Join-Path $output 'map-capture-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Map capture compilation failed' }
    & (Join-Path $output 'map-capture-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Map capture tests failed' }
    & $Compiler @flags (Join-Path $project 'tests/CaptureSelectionTests.cpp') -o (Join-Path $output 'capture-selection-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Capture selection compilation failed' }
    & (Join-Path $output 'capture-selection-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Capture selection tests failed' }
    & $Compiler @flags (Join-Path $project 'tests/UiTests.cpp') (Join-Path $project 'src/Ui.cpp') (Join-Path $project '../DeadIslandSecondScreen/src/Canvas.cpp') -lgdi32 -luser32 -o (Join-Path $output 'ui-tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'UI tests compilation failed' }
    & (Join-Path $output 'ui-tests.exe') (Join-Path $output 'test-fixtures')
    if ($LASTEXITCODE -ne 0) { throw 'UI tests failed' }
    & $Compiler @flags (Join-Path $project 'tools/RuntimeCapture.cpp') -lbcrypt -o (Join-Path $output 'Crysis3RuntimeCapture.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Read-only capture tool compilation failed' }
    & (Join-Path $output 'Crysis3RuntimeCapture.exe') 0 (Join-Path $output 'invalid-capture-must-not-exist')
    if ($LASTEXITCODE -ne 2 -or (Test-Path (Join-Path $output 'invalid-capture-must-not-exist'))) { throw 'Capture tool did not reject invalid PID safely' }
    foreach ($image in @('.research/crysis3/original/Bin32/Crysis3.exe','.work/crysis3/execute-permission/Crysis3.exe')) {
        $imagePath=Join-Path $workspace $image
        if (Test-Path $imagePath) {
            & (Join-Path $output 'Crysis3RuntimeCapture.exe') --check-image $imagePath
            if ($LASTEXITCODE -ne 0) { throw 'Capture tool rejected a verified research image' }
        }
    }
    # The deliberate rejection writes stderr; Windows PowerShell can promote
    # that expected native message to a terminating NativeCommandError.
    $savedErrorPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & (Join-Path $output 'Crysis3RuntimeCapture.exe') --check-image (Join-Path $output 'core-tests.exe') 2>$null
        $rejectionExitCode=$LASTEXITCODE
    } finally { $ErrorActionPreference=$savedErrorPreference }
    if ($rejectionExitCode -ne 6) { throw 'Capture tool accepted an unrelated executable' }
    & $Compiler @flags (Join-Path $project 'src/Preview.cpp') (Join-Path $project 'src/Ui.cpp') $touch (Join-Path $project '../DeadIslandSecondScreen/src/Canvas.cpp') -lws2_32 -lgdiplus -lgdi32 -luser32 -o (Join-Path $output 'Crysis3UiReview.exe')
    if ($LASTEXITCODE -ne 0) { throw 'UI compilation failed' }
    & (Join-Path $output 'Crysis3UiReview.exe') --render (Join-Path $output 'screens')
    if ($LASTEXITCODE -ne 0) { throw 'UI rendering failed' }
    Write-Host 'Offline development harness only. No payload built or installed; live adapter is unverified.'
} finally { $env:PATH=$previousPath }

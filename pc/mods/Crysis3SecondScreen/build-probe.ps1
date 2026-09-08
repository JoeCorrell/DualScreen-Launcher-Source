param([string]$Compiler='C:/msys64/mingw32/bin/i686-w64-mingw32-g++.exe')
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $MyInvocation.MyCommand.Path
$workspace=[IO.Path]::GetFullPath((Join-Path $project '../../..'))
$output=Join-Path $workspace '.work/crysis3/native-probe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$previousPath=$env:PATH
try {
    $env:PATH=(Split-Path -Parent $Compiler)+';'+$previousPath
    $flags=@('-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-static','-static-libgcc','-static-libstdc++')
    & $Compiler @flags (Join-Path $project 'tests/ProbeCallbackGuardTests.cpp') -o (Join-Path $output 'callback-guard-tests.exe')
    if($LASTEXITCODE -ne 0) { throw 'Callback guard compilation failed' }
    & (Join-Path $output 'callback-guard-tests.exe')
    if($LASTEXITCODE -ne 0) { throw 'Callback guard tests failed' }
    & $Compiler @flags (Join-Path $project 'tests/NativeProbeTests.cpp') -lbcrypt -o (Join-Path $output 'native-probe-tests.exe')
    if($LASTEXITCODE -ne 0) { throw 'Native probe tests compilation failed' }
    & (Join-Path $output 'native-probe-tests.exe')
    if($LASTEXITCODE -ne 0) { throw 'Native probe boundary tests failed' }
    & $Compiler @flags -DC3_NATIVE_UI -shared (Join-Path $project 'src/NativeProbe.cpp') (Join-Path $project 'src/Panel.cpp') (Join-Path $project 'src/Ui.cpp') (Join-Path $project '../DishonoredSecondScreen/src/Touch.cpp') (Join-Path $project '../DeadIslandSecondScreen/src/Canvas.cpp') (Join-Path $project 'src/dinput8-probe.def') -lbcrypt -lws2_32 -lgdi32 -luser32 -o (Join-Path $output 'dinput8.dll')
    if($LASTEXITCODE -ne 0) { throw 'Native probe DLL compilation failed' }
    Get-FileHash (Join-Path $output 'dinput8.dll') -Algorithm SHA256
    Write-Host 'Guarded, disabled-by-default native RESEARCH probe. Not installed; not the completed mod.'
} finally { $env:PATH=$previousPath }

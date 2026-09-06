param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$compiler='C:/msys64/mingw32/bin/i686-w64-mingw32-g++.exe'
$env:PATH=(Split-Path -Parent $compiler)+';'+$env:PATH
$out=Join-Path $root ('build/'+$Configuration.ToLowerInvariant())
New-Item -ItemType Directory -Force -Path $out | Out-Null
$common=@('-std=c++17','-m32','-Wall','-Wextra','-Wpedantic','-Werror','-Wno-misleading-indentation',
    '-DWIN32_LEAN_AND_MEAN','-DNOMINMAX','-static-libgcc','-static-libstdc++','-I',"$root/src")
if($Configuration -eq 'Release'){$common += '-O2'}else{$common += @('-O0','-g')}
& $compiler @common "$root/tests/ContractTests.cpp" '-static' '-o' "$out/ContractTests.exe"
if($LASTEXITCODE -ne 0){throw 'Contract test compilation failed'}
& "$out/ContractTests.exe"
if($LASTEXITCODE -ne 0){throw 'Contract tests failed'}
& $compiler @common "$root/tests/NativeReadTests.cpp" '-static' '-o' "$out/NativeReadTests.exe"
if($LASTEXITCODE -ne 0){throw 'Native read test compilation failed'}
& "$out/NativeReadTests.exe"
if($LASTEXITCODE -ne 0){throw 'Native read tests failed'}
& $compiler @common "$root/tests/RenderHarness.cpp" "$root/src/AnimusUi.cpp" -static `
    -lgdiplus -lgdi32 -luser32 -o "$out/RenderHarness.exe"
if($LASTEXITCODE -ne 0){throw 'Render harness compilation failed'}
& "$out/RenderHarness.exe" "$out/preview"
if($LASTEXITCODE -ne 0){throw 'Render harness failed'}
& $compiler @common -shared "$root/src/Runtime.cpp" "$root/src/AnimusUi.cpp" "$root/src/NativeBridge.cpp" "$root/src/dinput8.def" `
    -static -lgdiplus -lgdi32 -luser32 -lws2_32 -lbcrypt -lole32 '-Wl,--no-insert-timestamp' -o "$out/dinput8.dll"
if($LASTEXITCODE -ne 0){throw 'Native proxy compilation failed'}
$imports=& 'C:/msys64/mingw32/bin/objdump.exe' -p "$out/dinput8.dll"
if($LASTEXITCODE -ne 0 -or ($imports -join "`n") -match 'DLL Name: (libgcc|libstdc|libwinpthread)'){
    throw 'Invalid native dependencies'
}
Write-Output "Development native payload built at $out. Read-only native adapter; actions remain disabled. No device or launcher payload writes."

$ErrorActionPreference = 'Stop'
$compiler = 'C:/msys64/mingw32/bin/i686-w64-mingw32-g++.exe'
$env:PATH = (Split-Path -Parent $compiler) + ';' + $env:PATH
$out = Join-Path $PSScriptRoot 'build/probe'
New-Item -ItemType Directory -Force -Path $out | Out-Null
& $compiler -std=c++17 -m32 -O2 -Wall -Wextra -Wpedantic -Werror -DWIN32_LEAN_AND_MEAN -DNOMINMAX `
    -shared -static -static-libgcc -static-libstdc++ "$PSScriptRoot/src/Probe.cpp" "$PSScriptRoot/src/dinput8.def" `
    -lbcrypt '-Wl,--no-insert-timestamp' -o "$out/dinput8.dll"
if ($LASTEXITCODE -ne 0) { throw 'Dishonored diagnostic probe compilation failed' }
$imports = & 'C:/msys64/mingw32/bin/objdump.exe' -p "$out/dinput8.dll"
if ($LASTEXITCODE -ne 0 -or ($imports -join "`n") -match 'DLL Name: (libgcc|libstdc|libwinpthread)') { throw 'Invalid native dependencies' }
Write-Output "Built development-only game-thread probe: $out/dinput8.dll. Not a complete or release-ready payload."

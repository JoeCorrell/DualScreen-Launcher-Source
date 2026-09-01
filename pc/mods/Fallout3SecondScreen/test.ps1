$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$compiler = 'C:\msys64\mingw32\bin\i686-w64-mingw32-g++.exe'
$env:PATH = (Split-Path -Parent $compiler) + ';' + $env:PATH
$testRoot = Join-Path $projectRoot 'build\tests'
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
$testBinary = Join-Path $testRoot 'StateStoreTests.exe'

& $compiler '-std=c++17' '-m32' '-O2' '-Wall' '-Wextra' '-Wpedantic' '-Werror' `
    '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' '-static' '-static-libgcc' `
    '-static-libstdc++' ('-I' + (Join-Path $projectRoot 'src')) `
    (Join-Path $projectRoot 'tests\StateStoreTests.cpp') `
    (Join-Path $projectRoot 'src\StateStore.cpp') `
    (Join-Path $projectRoot 'src\Version.cpp') `
    (Join-Path $projectRoot 'src\Log.cpp') `
    '-ladvapi32' '-o' $testBinary
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }

& $testBinary
if ($LASTEXITCODE -ne 0) { throw 'Native unit tests failed.' }

$localMapTest = Join-Path $testRoot 'LocalMapFrameTests.exe'
& $compiler '-std=c++17' '-m32' '-O2' '-Wall' '-Wextra' '-Wpedantic' '-Werror' `
    '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' '-static' '-static-libgcc' `
    '-static-libstdc++' ('-I' + (Join-Path $projectRoot 'src')) `
    (Join-Path $projectRoot 'tests\LocalMapFrameTests.cpp') `
    (Join-Path $projectRoot 'src\LocalMapFrame.cpp') `
    '-o' $localMapTest
if ($LASTEXITCODE -ne 0) { throw 'Local-map frame test compilation failed.' }
& $localMapTest
if ($LASTEXITCODE -ne 0) { throw 'Local-map frame tests failed.' }

$mapProjectionTest = Join-Path $testRoot 'MapProjectionTests.exe'
& $compiler '-std=c++17' '-m32' '-O2' '-Wall' '-Wextra' '-Wpedantic' '-Werror' `
    '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' '-static' '-static-libgcc' `
    '-static-libstdc++' ('-I' + (Join-Path $projectRoot 'src')) `
    (Join-Path $projectRoot 'tests\MapProjectionTests.cpp') `
    '-o' $mapProjectionTest
if ($LASTEXITCODE -ne 0) { throw 'Map-projection test compilation failed.' }
& $mapProjectionTest
if ($LASTEXITCODE -ne 0) { throw 'Map-projection tests failed.' }

$binaryTest = Join-Path $testRoot 'BinaryPatternTests.exe'
& $compiler '-std=c++17' '-m32' '-O2' '-Wall' '-Wextra' '-Wpedantic' '-Werror' `
    '-DWIN32_LEAN_AND_MEAN' '-DNOMINMAX' '-static' '-static-libgcc' `
    '-static-libstdc++' ('-I' + (Join-Path $projectRoot 'src')) `
    (Join-Path $projectRoot 'tests\BinaryPatternTests.cpp') '-o' $binaryTest
if ($LASTEXITCODE -ne 0) { throw 'Binary-pattern test compilation failed.' }

$exactBinary = 'C:\Users\Shadow\AppData\Local\Temp\wemu-fallout3-recon\Fallout3-current.exe'
if (Test-Path -LiteralPath $exactBinary) {
    & $binaryTest $exactBinary
    if ($LASTEXITCODE -ne 0) { throw 'Exact Fallout binary tests failed.' }
} else {
    Write-Warning 'Exact user-owned Fallout executable is unavailable; binary-pattern tests skipped.'
}

foreach ($script in @('deploy.ps1', 'apply-performance-profile.ps1')) {
    $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $projectRoot $script), [ref]$null, [ref]$parseErrors) | Out-Null
    if ($parseErrors.Count) {
        $parseErrors | ForEach-Object { Write-Error $_.Message }
        throw "$script syntax validation failed."
    }
}
Write-Host 'Deployment script syntax passed'

. (Join-Path $projectRoot 'tools\Ini.ps1')
$iniInput = Join-Path $testRoot 'profile.input.ini'
$iniProfile = Join-Path $testRoot 'profile.patch.ini'
$iniOutput = Join-Path $testRoot 'profile.output.ini'
[IO.File]::WriteAllText($iniInput,
    "[Display]`r`niSize W=1280`r`niMultiSample=4`r`n`r`n[Audio]`r`nbUseAudioDebugInformation=1`r`n",
    [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($iniProfile,
    "[Display]`niMultiSample=0`nbDrawShadows=0`n[Audio]`nbUseAudioDebugInformation=0`n",
    [Text.UTF8Encoding]::new($false))
Merge-IniProfile $iniInput $iniProfile $iniOutput
$once = [IO.File]::ReadAllText($iniOutput)
Merge-IniProfile $iniOutput $iniProfile $iniInput
$twice = [IO.File]::ReadAllText($iniInput)
if ($once -ne $twice -or $once -notmatch 'iSize W=1280' -or
    $once -notmatch 'iMultiSample=0' -or $once -notmatch 'bDrawShadows=0' -or
    $once -match 'iMultiSample=4' -or $once -match 'bUseAudioDebugInformation=1') {
    throw 'INI performance profile merge test failed.'
}
Write-Host 'INI performance profile tests passed'

. (Join-Path $projectRoot 'tools\Registry.ps1')
$registryInput = Join-Path $testRoot 'user.reg.input'
$registryOutput = Join-Path $testRoot 'user.reg.output'
$sample = @'
WINE REGISTRY Version 2

[Software\\Wine\\AppDefaults\\Fallout3.exe\\DllOverrides] 123
"d3d9"="native"
"dinput8"="builtin,native"

[Software\\Wine\\DllOverrides] 456
"dinput8"="builtin,native"
'@
[IO.File]::WriteAllText($registryInput, $sample, [Text.UTF8Encoding]::new($false))
$prior = Set-FalloutDinputOverride $registryInput $registryOutput
$patched = [IO.File]::ReadAllText($registryOutput)
if ($prior -ne '"dinput8"="builtin,native"' -or
    $patched -notmatch '(?s)AppDefaults.*?"dinput8"="native,builtin"' -or
    $patched -notmatch '"d3d9"="native"' -or
    ([regex]::Matches($patched, '"dinput8"="native,builtin"').Count -ne 1)) {
    throw 'Wine AppDefaults replacement test failed.'
}
$sampleWithoutSection = "WINE REGISTRY Version 2`n`n[Software\\Wine\\DllOverrides]`n`"x`"=`"y`"`n"
[IO.File]::WriteAllText($registryInput, $sampleWithoutSection,
                        [Text.UTF8Encoding]::new($false))
$prior = Set-FalloutDinputOverride $registryInput $registryOutput
$patched = [IO.File]::ReadAllText($registryOutput)
if ($null -ne $prior -or
    $patched -notmatch '(?s)\[Software\\\\Wine\\\\AppDefaults\\\\Fallout3\.exe\\\\DllOverrides\].*?"dinput8"="native,builtin"' -or
    $patched -notmatch '"x"="y"') {
    throw 'Wine AppDefaults insertion test failed.'
}
Write-Host 'Wine registry patch tests passed'

$systemInput = Join-Path $testRoot 'system.reg.input'
$systemOutput = Join-Path $testRoot 'system.reg.output'
$systemSample = @'
WINE REGISTRY Version 2

[Software\\Wow6432Node\\Bethesda Softworks\\Skyrim Special Edition]
"Installed Path"="F:\\Skyrim\\"
'@
[IO.File]::WriteAllText($systemInput, $systemSample,
                        [Text.UTF8Encoding]::new($false))
$prior = Set-FalloutInstalledPath $systemInput $systemOutput
$patched = [IO.File]::ReadAllText($systemOutput)
if ($null -ne $prior -or
    $patched -notmatch '(?s)\[Software\\\\Wow6432Node\\\\Bethesda Softworks\\\\Fallout3\].*?"Installed Path"="F:\\\\Download\\\\Fallout-3-GOTY-SteamRIP\.com\\\\Fallout 3\\\\"' -or
    $patched -notmatch 'Skyrim Special Edition') {
    throw 'Fallout installed-path registry insertion test failed.'
}
$prior = Set-FalloutInstalledPath $systemOutput $systemInput
$patched = [IO.File]::ReadAllText($systemInput)
if ($prior -ne '"Installed Path"="F:\\Download\\Fallout-3-GOTY-SteamRIP.com\\Fallout 3\\"' -or
    ([regex]::Matches($patched, '\[Software\\\\Wow6432Node\\\\Bethesda Softworks\\\\Fallout3\]').Count -ne 1)) {
    throw 'Fallout installed-path registry replacement test failed.'
}
Write-Host 'Fallout installed-path registry tests passed'

$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$cache=Join-Path $env:USERPROFILE '.gradle/caches/modules-2/files-2.1'
$out=Join-Path $repo 'pc/mods/AssassinsCreedIISecondScreen/build/launcher-contracts'
New-Item -ItemType Directory -Force -Path $out | Out-Null
function Artifact([string]$group,[string]$name,[string]$version) {
    $directory=Join-Path $cache "$group/$name/$version"
    $result=Get-ChildItem -LiteralPath $directory -Recurse -Filter "$name-$version.jar" | Select-Object -First 1
    if(!$result){throw "Missing cached test dependency: $group/$name/$version"}
    return $result.FullName
}
$stdlib=Artifact 'org.jetbrains.kotlin' 'kotlin-stdlib' '2.1.20'
$coroutines=Artifact 'org.jetbrains.kotlinx' 'kotlinx-coroutines-core-jvm' '1.8.0'
$annotations=Artifact 'org.jetbrains' 'annotations' '13.0'
$compiler=@(
    (Artifact 'org.jetbrains.kotlin' 'kotlin-compiler-embeddable' '2.1.20'),
    $stdlib,
    (Artifact 'org.jetbrains.kotlin' 'kotlin-script-runtime' '2.1.20'),
    (Artifact 'org.jetbrains.kotlin' 'kotlin-reflect' '1.6.10'),
    (Artifact 'org.jetbrains.intellij.deps' 'trove4j' '1.0.20200330'),
    $coroutines,$annotations
) -join ';'
$junit=Artifact 'junit' 'junit' '4.13.2'
$hamcrest=Artifact 'org.hamcrest' 'hamcrest-core' '1.3'
$json=Artifact 'org.json' 'json' '20250107'
$android=Join-Path $env:LOCALAPPDATA 'Android/Sdk/platforms/android-35/android.jar'
$classpath=@($out,$stdlib,$coroutines,$annotations,$junit,$hamcrest,$json,$android) -join ';'
$jdk='C:/Program Files/Microsoft/jdk-17.0.19.10-hotspot/bin'
$sources=@(
    "$repo/launcher/src/main/kotlin/com/dualscreen/launcher/GamePayloadPackage.kt",
    "$repo/launcher/src/main/kotlin/com/dualscreen/launcher/OnDemandGamePayloads.kt",
    "$repo/launcher/src/test/kotlin/com/dualscreen/launcher/GamePayloadPackageTest.kt"
)
& "$jdk/java.exe" '-Xmx384m' '-cp' $compiler 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler' `
    '-no-stdlib' '-no-reflect' '-jvm-target' '17' '-classpath' $classpath '-d' $out @sources
if($LASTEXITCODE -ne 0){throw 'Payload contract compilation failed'}
& "$jdk/javac.exe" '-J-Xmx192m' '-cp' $classpath '-d' $out `
    "$repo/engine/src/main/java/com/wemu/engine/renderer/NativePanelIdentity.java" `
    "$repo/engine/src/test/java/com/wemu/engine/renderer/NativePanelIdentityTest.java"
if($LASTEXITCODE -ne 0){throw 'Panel routing contract compilation failed'}
& "$jdk/java.exe" '-Xmx192m' '-cp' $classpath 'org.junit.runner.JUnitCore' `
    'com.dualscreen.launcher.GamePayloadPackageTest' 'com.wemu.engine.renderer.NativePanelIdentityTest'
if($LASTEXITCODE -ne 0){throw 'Launcher contracts failed'}

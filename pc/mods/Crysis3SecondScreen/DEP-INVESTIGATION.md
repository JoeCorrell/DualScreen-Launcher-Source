# Tutorial crash / DEP investigation

## Latest live evidence

The installed versionCode 57 permission-trial executable reached owner-controlled
Jailbreak gameplay. The subsequent read-only native capture found the prior fault
address in committed PAGE_EXECUTE_READ memory. The owner retracted the later
crash alarm; heavy stuttering remains reported. See [NATIVE-RUNTIME.md](NATIVE-RUNTIME.md)
for hashes, evidence locations and limitations. Cross-mission/checkpoint stability
is not yet established. Historical pre-trial notes below do not describe the
current installed patch state.

## Captured execute fault and permission trial (2026-09-07 18:02 EDT)

The failed launcher run used Games/container 1, Proton 11 ARM64EC, FEXCore
2608 and DXVK 3.0. Android records Crysis PID 12290 dying at 18:02:48.670.
No current Android native tombstone, LMK kill, or graphics-device-loss report
was found in the collected system log. Socket errors after that exit are not
evidence of the original cause.

The persistent launch log incorrectly stopped at 17:58:57.511 when the first
game window matched: GameSessionActivity unconditionally detached its file
capture callback. The still-live lower-screen ring retained the final Wine
exception. Screenshots saved under `.work/crysis3/crash-20260907-current/`
show, at Wine timestamp 452921.236, process 00f4, thread 00f8:

```
dispatch_exception code=c0000005 (EXCEPTION_ACCESS_VIOLATION)
addr=39C169CE  info[0]=00000008  info[1]=39C169CE
eip=39c169ce
```

Information[0] = 8 identifies an execute/DEP violation, not a read/write fault:
[Microsoft EXCEPTION_RECORD](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-exception_record).
The fault immediately precedes game exit; no claim is made that every possible
mission crash shares this cause.

The exact original hash is `f332e2fb466ec8ce44336a4af06cd498bb5a787d7352548c76f2e4ec008f5e8e`.
PE parsing places VA 39C169CE at RVA 02C169CE inside `.data`, RVA 02B40000,
virtual size 02719FB0, Characteristics C0000040 (read/write, not execute).
Disassembly at the fault begins `mov edx,[ecx+4]; xor eax,eax; push esi;
test edx,edx; jle ...`, with subsequent bounded search/control-flow instructions.
These bytes exist on disk; no decrypted gameplay class/hook is inferred.

`tools/patch_execute_permission.py` parses the section table and builds a
hash-locked delta changing exactly file offset 025F from C0 to E0. This adds
IMAGE_SCN_MEM_EXECUTE to that section only; it does not alter instructions,
NX_COMPAT, global DEP, Wine registry settings, Skyrim's prefix, or saves.
It does permit execution throughout the approximately 39 MiB writable `.data`
section, a security tradeoff confined to this executable. The section permission
meaning is defined by [Microsoft PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).

Output hash: `fffd52e0c483b55f04e0048dff0c45dd445734cfcf6470aa3d84fbf84040e055`.
The 123-byte DSDP delta contains one literal permission byte and copy ranges,
not a redistributed executable. The existing atomic, full-hash-verified
payload installer applies it idempotently and rejects unknown input binaries.
The imported original and this one exact output are both recognized by the catalog.

An untouched, hash-verified device backup was created at
`/sdcard/Dualscreen/Games/Crysis 3/Backups/execute-permission-20260907/Crysis3.exe`.
The original ZIP and local research executable are unchanged. To roll back,
use the prior diagnostics APK (otherwise the new payload reapplies the bit),
then restore that original backup while Crysis is closed.

Diagnostics now retain the callback through gameplay, queue pipe output for a
background writer, rotate `guest-tail.log` / `guest-tail.1.log` instead of
discarding all output after the startup cap, and export the private native
`Game.log` every five seconds. `Game-before-launch.log` preserves its prior
contents. No runtime setting other than this EXE permission trial is changed.

Status: binary delta independently verified; tutorial/checkpoint replay is
still required before calling this a working crash fix.

The signed APK `1.6.17.27-crysis-execute-trial` (versionCode 56) built
successfully and its packaged patch/documentation hashes and preserved WFC
manifest were verified. APK SHA-256:
`ccecf9cdb80a11bcf67855883f4eb16e2b789ce62d08c1e07db8e1e399669f45`.
It is saved under `.work/crysis3/`. A first, unshipped package was rejected by
the final audit because Android unpacked a `.gz` asset; the corrected delta uses
the `.dsdp` suffix while retaining its expected gzip bytes and SHA-256.
29 launcher checks, 2 binary-delta checks and 5 native suites passed.
ADB was taken offline before that build could be installed.

### Installed rebuild (2026-09-07 18:51 EDT)

At the owner's request, reconnected to `100.83.63.70:41335`, rebuilt current
launcher sources as `1.6.17.28-crysis-update` (versionCode 57), and installed with
`adb install -r`. No uninstall or app-data clearing was performed. Release build,
29 selected launcher tests, six native suites, and 12 Python tests passed.
APK signature and both packaged Crysis payload hashes were verified; the existing
published WFC payload was preserved.

Artifact: `.work/crysis3/DualScreenLauncher-1.6.17.28-crysis-update.apk`.
APK SHA-256: `35e08d3387b8cad089993ba4a6dee8af6b5c0d6bc7877a8acfec2cbb2314e7bb`.
Android confirmed version 57 and update time 18:51:45. Opening the launcher
returned status `ok`; a bounded check of its current-process log showed no fatal
exception. This is an app-start check, not a gameplay stability test.

Before installation the device EXE and backup both matched the original hash.
After installation, the device game EXE matched the exact permission-trial output
hash above, confirming that the delta was applied. Tutorial/checkpoint replay,
live diagnostic retention, native hooks and bottom-screen gameplay remain
unverified. The offline UI harness was not deployed as a functional mod.

### First successful run reported (2026-09-07 18:56 EDT)

The owner reported "IT IS FIXED" during the monitored patched run. Crysis Linux
PID 21812 remained alive through the final check at 18:56:10; exported Game.log
reached Jailbreak loading and TPS extension registration. The captured current
guest logs contained no `c0000005`, unhandled/fatal error, device-loss message or
queue-overflow warning. Presentation counters continued increasing during the
watch, and guest output was retained beyond the first-window startup event.

Evidence is preserved under `.work/crysis3/monitor-20260907-185427/`, including
initial/intermediate/final (`successful-session`) exported logs and the Android
live stream. The PC-side live stream was stopped after saving the evidence;
the game was left running and built-in diagnostic logging was not disabled.

This establishes an owner-confirmed improvement in the previously failing run,
not comprehensive tutorial, checkpoint or cross-mission stability verification.
It does not verify any native dual-screen gameplay hooks.

## Earlier investigation (before an execute fault was captured)

2026-09-07: the owner identified WEMU's `dion` container. Read-only ADB
inspection resolves it to `com.wemu/files/imagefs/home/xuser-6`.

Observed configuration:

- Proton-11.0-5-arm64ec-6, FEXCore 2601, COMPATIBILITY preset.
- DXVK-2.6-2-gplasync-0, 30 FPS, async enabled.
- 1280x720, Turnip wrapper, VKD3D-3.0b-0 also configured.
- Game executable SHA-256 still matches the known build in README.md.
- The executable's PE DllCharacteristics is zero, so NX_COMPAT is already clear.
  This is not proof of the runtime DEP policy; clearing the same bit is a no-op.

The real container log is in
`.wine/drive_c/users/xuser/Saved Games/Crysis3/Game.log`. Its latest inspected
contents include successful renderer/audio/engine initialization, followed by
Tutorial and Jailbreak level loading. The Tutorial segment reaches default
materials and TPS extension registration; the Jailbreak segment ends at default
materials. There is no fatal exception code or memory-access type in this log.
The separate `%user%/Game.log` in the game folder contains only earlier startup
output and must not be treated as the complete crash record.

The installed i386-windows/kernel32.dll was read through ADB without executing
or modifying it. Its SHA-256 is
`86c7e8eab69856834c11238744046fa495b6b67fbb83ff4871a75a0398dac0ba`.
Export-directed disassembly identifies SetProcessDEPPolicy and
GetProcessDEPPolicy. Importantly, this GetProcessDEPPolicy implementation queries
the current-process pseudo-handle even when a different handle is supplied;
a separate monitor cannot safely claim that result is Crysis's DEP policy.
SetProcessDEPPolicy(0) forwards zero flags in this build. Its presence alone is
not proof that the Wine/native/FEX path accepts the requested change.

Relevant upstream references (not a substitute for this custom runtime):

- [Windows SetProcessDEPPolicy contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setprocessdeppolicy)
- [Wine kernel32 implementation](https://github.com/wine-mirror/wine/blob/master/dlls/kernel32/process.c)
- [Proton process execute flags](https://github.com/ValveSoftware/wine/blob/bleeding-edge/dlls/ntdll/unix/process.c)

## Next diagnostic run

`tools/Crysis3-Diagnostic.cmd` is a manually invoked diagnostic launcher using
the F: game path observed in this container's log. It sets WINEDEBUG only in its
own environment and the child game, and directs stdout/stderr to a new randomly
named log beside the script. It does not edit registry settings, DEP policy,
the container, saves, DLLs or the game executable. Launching the game itself can
perform its normal profile/save writes. Do not run concurrently with Crysis.

Open the script from WEMU's file manager inside `dion`, reproduce one tutorial
crash, then close the diagnostic command window. Inspect the resulting log
alongside the container's Game.log. Logging may affect timing. Wine/FEX output
redirection is not yet runtime-tested; if the file contains only the launcher
header and exit code, collect WEMU's Log page and live logcat as well.

No DEP workaround has been installed or proven. An access violation alone is
insufficient: distinguish execute faults from read/write faults and distinguish
handled first-chance exceptions from the fatal exception before choosing a fix.

## DualScreen Launcher integration

The `crysis3` catalog entry uses the exact same default preset key and Wine
prefix as Skyrim SE (`games`), as subsequently requested by the owner. This is
not WEMU's `dion` prefix. The direct target remains `Bin32/Crysis3.exe`; the
standalone fixed-F: diagnostic batch file is not used by the launcher.

The shortcut enables `WINEDEBUG=-all,+timestamp,+pid,+tid,+seh`. The runtime
merges this after the container environment, without changing the shared
container or global preferences. Existing guest-output callbacks feed the lower
display's Logs page and the persistent, bounded launch log:
`/Dualscreen/Games/Crysis 3/Logs/launch-latest.log` (2 MiB guest-output budget).
The lower display starts in diagnostic-console mode with native panel surface
and touch routing disabled. The bundled payload contains documentation only;
no game hook, executable patch, DEP change, or speculative action is installed.

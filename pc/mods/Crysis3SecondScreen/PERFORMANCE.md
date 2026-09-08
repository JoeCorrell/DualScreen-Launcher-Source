# Crysis performance update: 2026-09-07

## Observations before the update

The owner reported heavy stuttering in live gameplay. Read-only Android process
inspection found `start.exe` allowed on host CPUs 3-7 (`0xf8`), while Crysis PID
4519 and winhandler PID 4506 used 3-5,7 (`0xb8`). Some game worker threads had
narrower masks. All CPUs 0-7 were online; the foreground and top-app cpusets
allowed all eight. CPU 6 was therefore not excluded by those Android cpusets.
These are observations from a now-closed session, not reusable process IDs.

Code inspection found that the launcher previously set guest affinity only after
`ProcessBuilder.start()` returned. Wine could already have created descendants
before that update. This is a real inheritance race; it has not yet been proven
to be the sole cause of the observed missing core or stutter.

## Implemented changes

- Crysis shortcuts explicitly select `FEXCore` / `PERFORMANCE`, including
  migration of existing shortcuts at launch. FEX version and the shared Games
  container remain inherited; Skyrim's settings are not rewritten.
- An opt-in launch scope pins the spawning Android thread before child creation,
  verifies the effective mask, and restores that thread's prior mask immediately
  afterward. The Crysis path no longer applies a late PID-only affinity update.
  Other games retain their existing launch behavior.
- Wine topology and pre-launch affinity use one strict parser. The observed
  container list `3,4,5,6,7` becomes host mask `0xf8` and topology `5:3,4,5,6,7`.
  Malformed or single-core lists are rejected for this opt-in path.
- Failure to establish the requested mask aborts child creation with diagnostics.
  Restoration failures are logged. There is no per-frame affinity rewriting.

The existing Performance preset disables scalar/vector/memcpy/half-barrier TSO,
enables reduced x87 precision, multiblock and dynamic L1, and leaves L2 enabled.
This describes the local preset implementation, not measured speed or guaranteed
stability. The previous container already selected Performance; the explicit
shortcut setting alone may therefore make no speed difference.

## Verification

Release unit tests: 8 affinity policy/scope tests, 2 Crysis configuration tests,
and 3 shared-container materialization tests passed. Scope tests cover inheritance
ordering, parent restoration, launch failure, kernel restriction, refused affinity,
and unchanged default paths. JNI affinity masks now use unsigned bit shifts.

The read-only capture helper supports `--performance-auto`. It reports the
target's Windows process/system affinity and the helper's inherited, whitelisted
FEX/topology variables. It does not read the target's environment or prove its
translator configuration. The pre-update attempt ended without a report when
the game closed; no baseline environment measurement is claimed.

Release `1.6.17.29-crysis-performance` (versionCode 58) built successfully and was
installed in place through ADB `100.83.63.70:41335` after the owner saved and closed
the game. APK SHA-256:
`9fdd7095092fe7849fbfaf4a2a4f46e6cb491d42f6c4cad688313732d7cc88f9`.
The release certificate matches APK 57. All 123 WFC, 154 Mario player and three
Crysis asset entries match that APK byte-for-byte. Both native affinity exports
are present; the device game executable still matches the permission-trial hash.
The build emitted Kotlin metadata warnings from R8 but completed successfully.

Post-launch effective masks still require verification. A fresh gameplay run
must compare Wine's reported topology with Linux game/thread masks
and measure frame times. The native research DLL is deliberately not part of
this performance APK, so it cannot confound that comparison.

## Launch regression and correction

APK 58 failed before creating Wine at 19:55:20: the requested host mask was
`0xf8`, but `sched_getaffinity` returned `0xd8` (CPUs 3,4,6,7). The strict equality
check introduced in that APK threw an IOException. The game did not crash; it
never started. A later read of that launcher thread showed mask `0xdf` despite
online CPUs 0-7 and a top-app cpuset allowing 0-7. The vendor restriction's exact
source is not established, and no thermal or system scheduling policy is changed.

Linux permits successful affinity calls to be silently narrowed by the kernel
([Linux man-pages](https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html)).
The correction accepts a read-back subset with at least two CPUs and updates
only that child's `ProcessBuilder` environment to the matching Wine topology
before `start()`. For the observed case that is `4:3,4,6,7`. Invalid, unreadable,
single-core and out-of-request masks still fail safely; parent restoration and
the no-opt-in path are preserved. Restrictions can change later, so this is a
launch-time observation, not a promise of permanent core availability.

Regression tests cover both observed restricted masks (`0xb8`, `0xd8`), exact
child topology, unchanged FEX settings, unchanged default callers, invalid
readbacks and restoration. The supplied 600x900 Crysis cover is also included
unchanged as `art_crysis3.png` (SHA-256
`9fc836b4e14b68294b217409a48d1ca3136ce537513ce473ab397ba3e097c7c7`).

Combined release `1.6.17.30-crysis-affinity` (versionCode 59) built successfully
and installed in place via ADB. APK SHA-256:
`2f572072dcceca0ee10ca6e6253eaa0c521683c5b501ab8d37999aff0f137d08`.
All 17 selected release tests passed. The certificate matches earlier releases;
all 280 checked WFC/Mario/Crysis payload asset entries remain byte-identical to
APK 58. AAPT maps `art_crysis3` to `res/UJ.png`; its decoded RGBA pixels match
the supplied artwork exactly. The cover mapping and Crysis title are tested.
Post-install game launch and gameplay performance remain to be verified.

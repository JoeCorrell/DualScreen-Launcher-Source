# Contact-order landing candidate (dev.21)

This revision checks for a native landing before the airborne update and retains
the guarded check after it. A successful native transition request skips the
airborne update and returns directly to the original transition dispatcher. If
no transition is requested, the complete original update runs normally.

The check only applies to the current player's unchanged, typed free-fall
context, with descent active, no target/action mode, no pending transition and
an enabled, expired landing timer. It resolves the motion component through
`005eb010`, allowing the engine to refresh stale cache generations or indices.
An absent, wrong-owner or wrong-type component, empty contact array or invalid
array pointer rejects the additional check. The original native classifier
`0107a250(1,1,0)` still decides whether the contact qualifies, and the original
dispatcher performs the transition. There are no direct position, velocity,
animation-ID, grounded-bit or damage writes. GPR, flags and floating-point state
are preserved around the additional calls. Rebased images use the original
update only.

This targets a possible contact/update ordering gap; it does not establish the
root cause of the recorded falling stall. The snapshots contain accepted ground
contacts and expired timers, but do not show their values at the instant of the
native landing decision. Live resolution is unverified.

## Build and validation

`landing_order.S` assembles to 640 bytes at `0256e100`. Assemble with GNU `as
--32`, extract `.text` with `objcopy`, and regenerate `landing_order_code.py`
from those bytes. The object must have no relocations.

`patch_landing_order.py ORIGINAL NEW_OUTPUT` requires the exact original
SHA-256, never edits its input, retains the dev.18 contact-search correction,
and preserves original sections and overlay except the three documented call
sites and PE metadata. It does not distribute a Ubisoft executable.

Original: `23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92`

Output: `89c7bbb81d44b5bed37b9e71302689742933e9732d918c1f1405619ebd58c088`

Output length: 34,701,312 bytes.

`test_landing_order.py` runs the actual assembled shim in an x86 instruction
emulator. It checks pre-update landing, contacts appearing only after update,
native rejection, stale-cache resolution, missing contacts, invalid owners,
destroyed contexts, mode/timer/transition guards, relocated pass-through and
register/FP preservation. With `AC2_ORIGINAL_EXE` set, it also verifies the PE
checksum, layout, unchanged sections and overlay, retained contact patch and
rejection of unsupported/already-patched inputs. Native engine calls are
controlled fixtures in these shim tests; they do not emulate FEX or prove the
live symptom is fixed.

## Installation scope

Replace an executable only while AC II is closed, retain a verified original
backup, and check the installed hash. The WEMU copy and DualScreen Launcher's
copy are separate files. Updating one does not update the other. The launcher
and companion DLL must both recognize this new hash before its game copy can
be replaced. Container settings, controller configuration, audio and CPU
affinity are not changed by this patch.

# Current-player, vitals, and phase contract

Research for the exact captured i386 build only. The Windows PID 244 captures in
`.work/crysis3/live-200610` are historical: the user has since closed the game.
Never reuse their heap addresses in a new process. This contract does not assert
that a native callback or transition test has passed.

### Subsequent fresh-session observations

In the later C3R202900 session, the user reported live gameplay and the main
agent collected `.work/crysis3/live-202900/read3` through `read19`. Each selected
binary was independently checked against its JSON SHA-256 and `stable=true`.
The reads occurred separately over helper elapsed57,301–79,695ms, not atomically.

- Root address `0x3b9f99b0`, framework `0x3b8516a0`, context `0x3324c448`;
  context state5; authoritative channel, cache, and actor entity all `0x7777`.
- Actor `0x32bbb6f0`, expected vtable; decoded health1000/max1000. Normal-path
  predicate byte0 and refresh byte0. Suit `0x4fc71e18` points back to that actor.
- Mode0 at `+0x80`, channel0; current energy approximately98.594154/max100.
  Channel1 held100/max100. The cause of that small depletion was not isolated.
- Root-owned HUD `0x026b0b48` matched primary/interface vtables
  `0x399ef860/0x399ef84c/0x399ef844`. Cutscene byte `+0x6d` was0 and adjacent
  byte `+0x6e` was1, confirming that the adjacent flag must not be conflated.
- Framework pause byte0, environment active byte1, timer `0x09035448` with
  vtable `0x39952878`, enabled byte `+0x68`=1. The live timer slot `+0x18`
  contained `0x37ce8800`. Thus the independently sampled native update
  predicates indicate ready/active/not-paused, with cutscene flagfalse.

This closes fresh pointer/vtable/flag observations for that session. The phase
getter code bytes and controlled pause/cutscene/transition comparisons are
separate checks. These observations alone do not attest a callback generation,
permission for game actions, or production readiness. All heap addresses above
remain session-specific evidence, never release constants.

Subsequent reads37–40 in the same session matched the saved native code exactly:
timer getter `37ce8800/4`, pause query `3749c660/32`, environment query
`3749dc40/28`, and `3749eeb0/63` covering both context predicates. Their JSON
SHA-256s and `stable=true` were independently checked.

After the user explicitly confirmed pausing, reads41–49 observed the same
root/framework/context/HUD/timer ownership. Framework byte `+4` changed from0
(read4) to1 (read43); context state remained5 (read44), environment active1
(read46), timer enabled1 (read48), and HUD cutscene0 (read49). This validates a
controlled false-to-true transition of the native pause expression through the
framework byte, without conflating it with a stopped timer or cutscene. The
paused sample sequence took helper elapsed158,086–167,928ms and was not atomic.
Resume and further gameplay transitions require their own later observations.

## Required provenance and reader gates

Source EXE SHA-256:
`fffd52e0c483b55f04e0048dff0c45dd445734cfcf6470aa3d84fbf84040e055`.
Image base: `0x37000000`. The exact loaded text, rdata, and header provenance is
recorded in `NATIVE-RUNTIME.md`; the reader's `exactProfile` guard must include
those build/layout gates and the code ranges listed below. A vtable-address match
alone is not sufficient to set that guard.

`src/NativeActorRead.h` copies values using a supplied exact-length read function.
It invokes no engine functions and writes no engine memory. Its guard requires
an attested callback thread, matching current thread, nonzero epoch, and no active
system event. It performs two complete reads and accepts only equal identity and
vitals tuples with an unchanged guard. Failed reads clear the output. This catches
observed changes, not arbitrary ABA allocation reuse. A proven owning callback
scope remains necessary; matching pointers alone do not prove lifetime.

The actor output has no pointers, adapter-verification bit, or inferred gameplay phase.
The caller must not retain a native actor pointer or call its methods from a UI
worker. A consistent actor does not establish whether a menu/cutscene is active.

## Authoritative normal current-player path

1. Global `0x3b9fde30` is the current root, primary vtable `0x399bef80`.
   Root virtual `+0x5c`, target `0x3802f430`, returns root `+0x30` framework.
2. Framework primary vtable `0x398ba5e0`, virtual `+0x1a0` target `0x3749e9b0`,
   loads framework `+0x28` context and tail-jumps to `0x37486e90` if non-null.
3. The getter first requires context `+0x24`, that object's `+8`, and the latter
   object's entity DWORD `+0x10` to be nonzero. This is the authoritative ID used
   to validate the cache, not the cache's own ID.
4. It reads an object at `[[context+0x2c]+0x3c]` and invokes virtual `+0xa8`.
   Observed vtable `0x3991ca90` gives `0x385eeb80`, exactly `8a 41 08 c3`:
   `mov al,[ecx+8]; ret`. Only the low byte of object `+8` is the predicate.
5. If that byte is nonzero, the getter uses a different path through global
   `0x3b80643c` virtual `+0x5c`, result virtual `+0x4c`, then result virtual `+0x2c`.
   Its ownership semantics are not resolved. **The read-only reader rejects this
   branch; it does not silently fall back to the normal cache.**
6. On the normal branch, the native function refreshes context `+0x8b0/+0x8b4`
   when the authoritative ID differs OR byte `0x3b806517` is nonzero. Refresh
   obtains the actor via global `0x3b851088` virtual `+0x4c`, then virtual `+0x0c`
   with the entity ID. The reader rejects either refresh condition; it neither
   mutates the cache nor assumes the previous pointer remains valid.
7. A non-null cached actor must have vtable `0x399b5940` and actor `+8` matching
   the authoritative channel ID.

Historical fresh-session evidence: read4 context, read6 holder, read8 channel,
read9 environment, read10 refresh byte, read13 predicate object, read21 predicate
vtable slot, and read22 predicate code. They observed matching ID `0x7777`,
refresh byte zero, and predicate byte zero. Upper predicate DWORD bytes were
`0x6c000000`; treating the DWORD as a boolean would incorrectly take the alternate
branch. Read21/22 matched the saved function, but this is not a lifecycle test.

## Health and suit values

Health getter `0x3804d9c0` (actor virtual `+0x64`) returns zero if actor `+0x28`
is zero; otherwise reinterpret `(DWORD[actor+0x24] XOR DWORD[actor+0x28])` as
float. Max-health getter `0x370d8f00` (virtual `+0x70`) returns float `actor+0x2c`.
Virtual `+0x68` is a different getter, not maximum health.

Suit owner is actor `+0xfc0`. A null owner permits health-only copied values;
the static fallback returned by virtual `+0x1d4` is not a real owned suit. A
non-null owner's `+0x18` must point back to the actor. The actor's suit allocation
and constructor were previously traced in `NATIVE-RUNTIME.md`.

**Correction: mode is owner `+0x80`, not `+0x84`.** Setter `0x381684f0` stores
the accepted mode at `0x381685ff`. Publisher `0x38168070` loads `[owner+0x80]`
at `0x38168086`, pushes it, and calls each listener virtual `+4` at `0x38168090`.
The already traced mode publisher maps 0 Tactical, 1 Armor, 2 Stealth. Field
`+0x84` participates in a different variant/state path and is not a mode source.
This corrects an earlier diagnostic observation, not an installed game hook.

The selected energy channel is owner `+0x1b8`, limited to 0 or 1. Energy is float
`owner+0x88+channel*0x70`. Its native maximum is float
`owner+0xc8+channel*0x70`, **not a hardcoded 100**:

- Native clamp `0x3816d2e0` limits `[this]` to `[0,this+0x40]`; caller
  `0x3816a6a6` starts at owner `+0x88` and the loop advances `0x70`, exactly twice.
- Normalization at `0x3816bd27..0x3816bd46` divides current energy by the maximum
  at owner `+0xc8+channel*0x70`.
- Config parser `0x3816e310` reads literal `maximumEnergy` at `0x399bad58` into
  the first float of a block copied to stats `+0x98/+0x108`. Initialization call
  `0x3816c299` passes stats = owner `+0x30`. Both map to owner `+0xc8/+0x138`.
  This is a traced config/initialization path, not a claim that no other writes
  to the field can occur. The reader always reads the current runtime maximum.

Channel policy is conditional. `0x38167260` is true when
`[global0x3b9fde34+0xb4]==1`, or when it equals 2 and byte `0x3b8064d1` is nonzero.
Only when that predicate is true does the mode setter assign channel = (mode==2).
Do not equate channel 1 with Stealth unconditionally or label it a new resource.
Historical read12 observed mode0, channel0, energy approximately53.4022/max100;
the other channel contained approximately96.4793/max100. Sparse observations do
not establish which gameplay actions caused either value.

## Native framework and cutscene predicates

`ReadCurrentPhase` copies these code-equivalent **state observations**, using the
same bounded reads and callback guards. They are not a fully tested UI-phase
state machine. Null HUD ownership produces `cutsceneKnown=false`, not false
cutscene state. `NativeUpdatePredicatesPass()` intentionally describes the three
native update predicates only; it does not authorize gameplay touch commands:

| Native query | Exact expression |
|---|---|
| Framework virtual `+0xc0`, `0x3749eeb0` | context exists and DWORD context `+0x858` equals5 |
| `0x3749eed0` | context exists and `0 < state < 5` |
| Framework virtual `+0x130`, `0x3749dc40` | context/environment exist and BYTE environment `+0xa9` is nonzero |
| Framework virtual `+0x1f4`, `0x3749c660` | BYTE framework `+4` nonzero OR timer virtual `+0x18` returnsfalse |
| Timer vtable `0x39952878`, virtual `+0x18`, `0x37ce8800` | BYTE timer `+0x68`; timer comes from global `0x3b8063dc` |

The timer getter is exactly `8a 41 68 c3`; its result is not timer `+4`.
Framework update `0x374a5550` gates update work using `+0x130`, state5, and the
negation of `+0x1f4`. CGame update `0x38189340` also gates gameplay-update work
using the negation of `+0x1f4`. These establish native update predicates. They do
not distinguish a loading image, in-game menu, checkpoint restore, or cutscene
by themselves. Context state5 follows the nonblocking connection/establishment
state machine at `0x37495c30`; labels for numeric states are not guessed.

Historical read39 saw environment `+0xa9`=1, read40 framework `+4/+5`=0,
read41 context state5, read38/42 timer pointer/vtable. The timer `+0x68` and the
fresh timer-code/vtable-slot checks were not captured before the user closed the
game. Those remain fresh-runtime checks; the predicate is statically verified.

Cutscene ownership is statically traced, not guessed from an adjacent object:
initialization call `0x3818b723` calls constructor `0x382befd0`, and
`0x3818b72e` stores its result at root `+0x1e8`. Constructor sets primary vtable
`0x399ef860`, interface `+4` vtable `0x399ef84c`, and interface `+8` vtable
`0x399ef844`. Begin callback `0x382bec00` receives the +4 interface, stores byte
`receiver+0x69`=1 (thus primary `+0x6d`), and publishes ID `0x113`.
End callback `0x382bede0` subtracts4 from its receiver, calls reset `0x382bed00`,
and publishes ID `0x114`. Reset writes WORD primary `+0x6d`=`0x100`, clearing
the cutscene byte and setting the next flag. Event initializer pairs at
`0x3ba047e0/+4` and `0x3ba047e8/+4` name these OnBeginCutScene/OnEndCutScene.
Fresh owner/vtable/name-table reads and actual enter/exit transitions remain
required before this predicate can authorize gameplay touch commands.

Menu open/close event candidates (`0x101/0x102`) publish at
`0x3837dbde/0x3837df31`. Their initial-state ownership and semantic relationship
to native pause are not yet resolved; no menu-state offset is adopted.

## Exact code fingerprints

Lengths are bytes; all hashes are SHA-256 of saved loaded text. Ranges denoted
window are deliberate instruction windows, not inferred complete functions.

| Address (hex) | Length (hex) | SHA-256 |
|---|---:|---|
| `3802f430` | `4` | `c8cc0b90c656bd6a43ec1c5684c1a494142d2fce4803153d9b1a0944cad7e281` |
| `3749e9b0` | `f` | `cfcdd04833f49b359500f06df6acc58c27933c14719634a6d49e0f7acb24b86c` |
| `37486e90` | `94` | `4fbcd8de3787b36f32f2db2909b79fa30547b0dd0381a1098c75a623d17b0def` |
| `385eeb80` | `4` | `8a905797df24e4a1fe68a2f630971aa55b606cd6a92fed57303379d0f93a7a2d` |
| `3804d9c0` | `20` | `89156db0cdac547ffa5e4bd1f91f7321c6918632331f16847386b2756344a9f8` |
| `370d8f00` | `10` | `f7ad03246f1eb8d3f73103e6bc87a24fa7af348e86607db825535159a1ec976d` |
| `38168070` | `7e` | `ca5fe84e3b9d7d6eb79724b8093a741104a1df2984b7d26e1ac21e25a4b3a027` |
| `381684f0` | `19a` | `80fd90eba106d598cd87bee8d639126c6fe0e0e5f6377e86c550892401657e68` |
| `38167260` | `27` | `734fa3e848b2e62a6348817f0ac1faed63f5aef11874be8b439e3c1e97267734` |
| `3816d2e0` | `83` | `7f77c926a7a3a00ff01e5259026f7a3c91d3eb9244c9c797c9c4b01837bc21f9` |
| `3816e310` | `19f` | `43099c92e6c28185de51219de6351d418b7360445c8f220f85b7b3d455aae364` |
| `3749eeb0` | `19` | `b14f90471f53e0e132b0810b437b3fb68b16f2660a444d27aba72b975516054d` |
| `3749eed0` | `1f` | `2347857d736193ff5707b7d6b4e9211ba6184735dd53af1eea7933b78bd39a75` |
| `3749dc40` | `1c` | `617183159ccd53e407c73909bfb1388cd82ccda0d71c649c3759b5da22ea2824` |
| `3749c660` | `20` | `53a78d712205162c655389baf6f1fca1a4300cd3f1ab6310fbda905a49003abb` |
| `37ce8800` | `4` | `fcbf383155b3c946776d097d9104628a7375812784049a6842b7cb665ea1aef7` |
| `382bec00` | `f5` | `447b87227828b119ba2888dda7c1ddf69d01b302a218dbeaabdb3f01e569cca0` |
| `382bed00` | `da` | `056ee41d841da487b1cb45f7620941887d164982880e65ea82bee7e4fe226bfb` |
| `382bede0` | `6f` | `e68426440b94086729871bfa2e52354d3b59a754e9b0a4c9517c7690dcc90a7b` |
| `382befd0` window | `50` | `8f5ffe3ae902fa496c1f23b73be97fafebdfadcdbb949991258961c929c409e6` |
| `3818b721` window | `13` | `ae98347b26fde25624db60a50b0e8d0bd1177b18710e95db8a61dca42ef2877e` |

Reports: `actor-energy-contract.txt`, `actor-energy-channel.txt`,
`actor-energy-max.txt`, `actor-phase-framework.txt`, `actor-phase-state.txt`, and
`actor-phase-cutscene.txt` in `.work/crysis3/live-190400`. Decompiled prototypes
and stack arguments are inferred: instruction checks take precedence. In
particular, the attempted `3816a650` entry in actor-energy-max.txt landed inside
an instruction and its pseudocode is **rejected**, not evidence. The valid clamp
caller instruction window starts at `3816a69b` in the owning earlier function.

## Test status and remaining gates

`NativeActorReadTests.cpp` passes 40 synthetic cases under MinGW32 `-O2 -Werror`,
including wrong mode-offset regression, selected channel1, native maximum125,
zero health key, null suit, predicate low-byte semantics, forced refresh, pointer
and epoch changes, invalid numeric values, unsupported code, and wrong thread.
Phase cases cover timer pause, framework pause short-circuit, low-byte semantics,
native context predicates, cutscene flag and adjacent-byte separation, unknown
HUD, unsupported owner layout/code, and transition changes during a read.
`ProbeCallbackGuard.h` supplies separate nonallocating, lock-free callback
bookkeeping, with 13 synthetic cases in `ProbeCallbackGuardTests.cpp`. An update
scope starts before forwarding the original call; only its outermost same-thread
scope may pass `CanObserve()` after return. Call that check before and after
copying state. A foreign update thread permanently rejects further observations;
the helper never automatically re-attests it. System scopes retain immutable
entry observation epochs for journaling and unwind their depth using RAII.
Epoch overflow disables observations. These observation epochs invalidate copied
data after any tracked system event; they are **not lifecycle generations**.
Original native calls must always be forwarded regardless of these diagnostic
gates, and root/profile/lifetime/phase checks remain the adapter's responsibility.
Synthetic tests cannot attest a game thread, transitions, observed damage, mode
switches, callback performance, or gameplay safety. The first guarded callback
probe must remain diagnostic until those live checks pass. Any later runtime
check must use new session pointers and exact code/profile checks.

# Suit input actions: static chain, not yet enabled

The user subsequently authorized a combined workspace build and installation.
These controls remain source/research changes only: none has been enabled and
no game input has been sent. The last Crysis gameplay process was closed by the
user, not a crash.

## Exact action route

`tools/audit_suit_actions.py` requires the same full captured text/rdata SHA-256
identities as ACTOR-CONTRACT.md. It verifies instruction bytes and call targets,
not guessed symbols. `live-190400/suit-input-actions.txt` contains independent
Ghidra output; assembly overrides its incomplete argument recovery.

| Native action | Name construction | Action object field | Registered callback |
|---|---|---|---|
| nanosuit_armor | 38193d07 | +28c | 380f2b30 |
| nanosuit_stealth | 38193d17 | +290 | 380f2be0 |

The extracted defaultProfile.xml defines both onPress and onRelease. Both
callbacks only perform the suit work when their **third stack argument is 1**.
Both end in `ret 0x10`, not the three arguments inferred by Ghidra. They return
AL=0 even after applying an action: this is **not a success acknowledgement**.
The other arguments' full dispatch contract remains unverified.

Registration at 380f6ea0/380f6eb7 passes the callback and action object to
382b2ac0. That routine manages native reference-counted strings and a tree.
Do not manufacture an ActionId from a raw character pointer or register
duplicate input handlers for the bottom screen.

## Receiver lifetime

Constructor 380f6780 assigns primary table399b63b8, secondary399b63a8 at+4,
and stores its player argument at+2d8 (380f6884). Player allocation branches
380d94a5 and380d95b5 allocate0x3f8, pass the player to this constructor,
then store the receiver at actor+1108 (380d9529/380d9658).
The replacement routine380d9580 destroys the previous receiver, clears+1108,
and selects among several receiver types. A player remaining alive does not
make its input receiver a persistent handle.

`NativeSuitInputRead.h` supplies a bounded copy-only ownership preflight with
root/player identity, exact receiver tables, back-reference, second copy and
epoch checks. It exports only presence, no address or action capability.
It is **not integrated into NativeProbe** pending full profile attestation and
live ownership verification. Its C++ tests passed in the combined68 native
development run; those are synthetic tests, not live receiver validation.

## Native restrictions and effects

Both callbacks consult actor vtable+80 ->380421b0 (decoded health <=0) and
+f8 ->380efcd0 (actor byte10e9; semantic name not yet established). They stop
when either predicate is true. Actor+1c4 ->380efc90 returns the suit at+fc0.

Armor calls380f0e10(1), Cloak calls380f0e10(2). The common helper checks suit
presence, suit+240 !=1, global[3b9fde34]+4a8 ==0; matching current mode switches
back to0. Switching to another mode additionally requires suit+84 ==0.
It invokes3816a090(mode,0); it does not merely write suit+80. The downstream
path has additional validation and local/network behavior.

Armor additionally checks active-channel energy after the helper and dispatches
an event with type0x15 through actor+1d0 ->380d3340 when appropriate. Directly
calling the shared helper would omit this behavior. The event's complete
meaning remains unlabelled until traced.

Before touchscreen activation: resolve current receiver anew on the accepted
game callback; prove the full dispatcher ABI/filtering and safe ordering;
validate current phase and restrictions; prevent duplicates and stale queued
taps; observe resulting native mode/energy rather than using callback AL as an
acknowledgement. Purchases/attachments require their own contracts and are not
authorized by any of this evidence.

## Command transport groundwork

`CommandInbox.h` adds a single-pending-command, nonblocking transport with
generation/sequence/capability/freshness checks. Request IDs are consumed even
when a full inbox rejects them; cancellation retains replay protection. Taking
a request removes it before validation/dispatch, so failure cannot implicitly
retry a toggle or purchase. It contains no engine calls and is not yet connected
to Panel or NativeProbe. The sender must use authoritative snapshots and the
native consumer must independently revalidate fresh game ownership and phase.

`CommandInboxTests.cpp` covers duplicates, overflow/retry, cancellation, stale
sequence/time, pause, revoked capability, read-only trial, forged keys, future
timestamps and generation changes. The C++ cases compiled and executed
successfully in `combined68-native-tests.log` after the build hold was lifted.
The existing45 Python tests also passed. Neither establishes a live action ABI.

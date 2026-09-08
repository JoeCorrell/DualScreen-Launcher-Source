# Native statistics: verified read-only path, live publication test pending

Source `NativeStatsRead.h`; offline audit `tools/audit_stat_tables.py`.
The game is not patched by the audit. No offsets were inferred from string
ordering alone: native name getters and value getters establish the mapping.

## Index and owner evidence

- Exact original EXE SHA256: `f332e2fb466ec8ce44336a4af06cd498bb5a787d7352548c76f2e4ec008f5e8e`.
- Integer name getter `3840ca00`: `[index*4+3b826930]`.
- Float name getter `3840ca10`: `[index*4+3b826d38]`.
- Integer value getter `3840cfb0`: `[this+22c+index*4]`, RET4.
- Float value getter `3840cfc0`: FLD `[this+634+index*4]`, RET4.
- The table-hashing loop at `3840b580` covers 258 integer and 25 float names.
  Three integer names start `eIPS_`, not `EIPS_`; do not truncate at index199.
- Original disk data contains these pointers. Exact live read6 matches all
  1132 bytes, SHA256 `8d86b987c6ec32e866f477d28baaa550c55cc93658bea39b1a64bbdccc34990f`.
- Root+`d4` stats owner was already established by the suit-point path.
  Constructor `3841d8d0` sets vtable `39a1a6b4`; instruction `3841db1b`
  publishes the same owner to `3ba0ba50`. Both owners must agree.

## Controlled session C3R234100

Linux game PID32521; root `3b9f99b0`; fresh stats owner `332274c8`.
These heap addresses are evidence only, never runtime constants.
Reads1-4 establish root/global/vtable agreement. Read5 copies native counter
block `owner+22c`, 1132 bytes. Read6 verifies the name table. Reads7/9
recheck owner; reads8/10 compare counters after owner-confirmed gameplay.

Between read8 (127327ms) and read10 (249812ms), the user walked/sprinted and
manually toggled Armor/Cloak:

| Counter | Before | After |
|---|---:|---:|
| Armor activations, integer117 | 24 | 26 |
| Tactical suit time, integer157 | 2489 | 2604 |
| Armor suit time, integer158 | 272 | 275 |
| Cloak suit time, integer159 | 107 | 111 |
| Distance run, float1 | 6084.1924 | 6213.7622 |
| Distance sprinted, float2 | 2785.2632 | 2832.6663 |

The three suit-time deltas total122, matching the elapsed122.485 seconds.
Stored `TimePlayed` (integer162) did not change at all between read5/read8,
then changed by99 during this later122-second interval. It is NOT published as
a live mission timer. `StealthActivations` did not increment in this test,
so it is omitted pending semantic investigation. No derived total-kill counter
is fabricated from potentially overlapping statistics.

The reader emits17 exact selected counters under `GAME COUNTER`, not `MISSION`.
Distance is labelled `game units` until world-unit scaling is independently
validated. Recorded headshot kills13 and hacks completed7 also replay correctly.

## Runtime integration and safety

Nine exact fingerprints cover owner access/construction/global publication,
native getters, vtable and live name table. Existing exact-image and actor-phase
guards still apply. Read only on Status at existing telemetry cadence, double
copy owner/counters, recheck epoch and outer actor/context. Results contain no
pointers and grant no actions. Invalid owner, changing data, non-finite floats,
out-of-range values or unknown profile clear the output.

Native and Python regressions pass; the DLL builds. This is not yet an installed
and verified UI feature: next launch must accept the stats profile and publish
flag0x100, then UI values must be compared with live gameplay. Capture request18
timed out after the game process disappeared; the user confirmed they closed
the game. Do not classify this as a crash or reuse old heap addresses.

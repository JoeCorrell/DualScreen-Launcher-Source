# Native schema-1 state ingestion

`src/LiveState.h/.cpp` is the allocation-conscious native consumer for the
GML bridge described in `GML_BRIDGE_PROTOCOL.md`. It is independent of the
panel renderer so the panel can copy a coherent typed snapshot without owning
file handles or JSON parser state.

## Discovery and I/O

`LiveStateIngestor` accepts the exact GML `CONFIG_DIRECTORY` when it is known.
With no explicit path it checks `FOMDS_STATE_FILE`, then
`FOMDS_CONFIG_DIRECTORY`, two direct `LOCALAPPDATA` candidates, and finally a
bounded `LOCALAPPDATA` walk (depth 5, 20,000 directory entries, 16 results).
The walk ignores reparse points. This is a discovery mechanism, not save-file
polling; only a `dualscreen/state.json` bridge file is considered.

State reads use one handle with read/write/delete sharing, an exact pre/post
size check, a 4 MiB cap, and reusable storage. The current and `state.old.json`
paths have independent file-identity/size/write-time caches. A missing,
partially replaced, or malformed current file therefore falls back to the
previous atomic-write generation without repeatedly parsing unchanged files.

`PollAcknowledgement` applies the same policy to `ack.json` and
`ack.old.json`, with a 16 KiB cap. It never claims, renames, truncates, or
deletes an acknowledgement.

## Validation and merge contract

- Protocol must be `fields-of-mistria-dualscreen`; schema must be exactly 1.
- JSON and decoded Unicode must be valid UTF-8; nesting is capped at 32.
- Sequence/revision integers must be non-negative, integral, and no greater
  than JavaScript/GML's exact integer ceiling (`2^53-1`).
- A full snapshot requires all 20 typed schema-1 topics, all 20 revisions,
  `base_sequence == 0`, and an empty set of known base revisions.
- A delta requires the cached epoch, exact base sequence, the next sequence,
  and exact base revisions for all typed topics. Changed topics must advance;
  unchanged topics must retain their revision. Any gap returns
  `kNeedsResync` without changing the published cache.
- Unknown topic and revision names are bounded-skipped. This permits additive
  schema-1 topics while keeping all 20 currently typed topics strict.
- Fixed arrays bound inventory, armor, item tags, skills, perks, effects,
  authored map pages/hubs/locations/markers, and quest requirements/targets/
  rewards.
  Oversized arrays fail closed. Display strings are UTF-8 decoded into fixed
  buffers and expose truncation; identity fields such as epoch fail rather
  than truncate.
- `player` exposes exact coordinates/facing plus optional mine fields; inactive
  mine fields remain explicitly absent rather than becoming zero-valued data.
- `map` retains only the bridge's source-authored coordinates and flattened
  page/hub/marker ranges. It requires `authored_coordinates == true`, refuses
  a continuous projection claim, and enforces anonymous unknown-NPC markers
  and locked-page hub suppression.
- Inventory and armor retain exact instance fingerprints, equipment state,
  required/prototype tags, safe-consume classification, source asset/LUT
  names, and museum flags. Fingerprints are bounded and may not truncate.
- `villagers` is capped at 128 met-and-unlocked entries and retains the exact
  icon, current neutral portrait, offset, and RGB card background. The parser requires
  `discovery_policy == unlocked_and_met_only` and
  `gift_preferences_exposed == false`; it has no gift-preference fields.
- `animals` is capped at 128 livestock entries plus one optional pet. Location,
  daycare, and attention values retain their source absence. The parser checks
  the bridge's daycare suppression and food/petting-derived need invariants.
- `magic` retains explicit source enum indexes (never inferred array indexes),
  source card assets, optional ID/index pin state, and bounded indexed
  cooldowns. Its exact three supported action names are validated.
- `almanac` and `museum` use flattened bounded category/wing/set/item storage,
  retaining stable range indexes without per-update heap graphs. Their
  unknown-entry and future-reward privacy flags must remain false.
- `renown` retains exact progress/rank values, optional next-rank presence, and
  the real claimable-reward inventory. Future rewards and actions must remain
  unexposed.
- `calendar_events` is capped at 512 events and retains the 28-day source
  horizon fields. Private-date availability and unreleased festivals must
  remain unexposed.
- `ack.json` additionally requires every protocol field, the currently cached
  epoch, nonzero exact state/command sequences, and nonempty bounded action and
  result code. A successful read returns the existing `BridgeActionAck` type
  consumed by `ActionTransport`.

`CopyLatest` uses an SRW lock and performs a value copy of the POD snapshot;
callers never retain parser buffers or references that can be invalidated by a
later delta.

`ActionTransport` exposes typed queues for hotbar selection, slot swapping,
armor equip/unequip, safe consumable use, spell pin/unpin, and pinned-spell
casting. Each queue checks the corresponding validated topic revision,
capability, source index, and (for item mutations) bounded opaque instance
fingerprints before the one-outstanding-command writer can publish it. Spell
actions use only the explicit learned enum indexes carried by `magic.spells`.

## Current limitation

The GML-to-host `CONFIG_DIRECTORY` mapping still needs runtime confirmation in
the user's FEX/Wine prefix. Environment overrides exist for deterministic
integration and do not weaken protocol/schema checks. This component does not
write `command.json`; resync command generation remains the action
transport/orchestration layer's responsibility.

`tests/LiveStateTests.cpp` covers full parsing, rich populated and empty
inventory/armor, authored maps, detailed quests, source visual metadata,
Unicode decoding, status-effect null slots, unknown additive topics/revisions,
delta merge, gap rejection, metadata no-change behavior, partial-current
fallback, acknowledgement validation/fallback/no-delete behavior, and the ack
size cap. It is run by `build.ps1` before the proxy is linked.

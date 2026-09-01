# Native action-command transport

## Status

`src/ActionTransport.{h,cpp}` implements and unit-tests the native-to-GML
command writer. It is connected to the validated state reader and the custom
panel, built into the proxy, and installed on the exact device target. No live
runtime mutation is claimed until it is exercised in a running save.

## Supported surface

There is no generic string-based action API. Native callers can request only:

- `QueueResync()` (transport recovery only; never a gameplay mutation)
- `QueueSelectHotbar(index, inventory_revision, hotbar_revision)`
- `QueueSwapSlots(first, second, inventory_revision)`
- `QueueEquipArmor(inventory_index, armor_index, expected_item, expected_equipped_item, inventory_revision, armor_revision)`
- `QueueUnequipArmor(armor_index, inventory_index, expected_item, expected_destination_item, inventory_revision, armor_revision)`
- `QueueConsumeItem(index, expected_item, inventory_revision, hotbar_revision)`
- `QueuePinSpell(spell, magic_revision)`
- `QueueUnpinSpell(spell, magic_revision)`
- `QueueCastPinnedSpell(spell, magic_revision)`

All require a validated schema-1 `BridgeActionSession`, an in-game session,
and a current epoch. Gameplay requests additionally require the exact advertised
capability and current topic revisions. Slot actions require current bounded
indexes; item mutations require bounded non-truncated UTF-8 fingerprints;
consumption requires the current selected hotbar slot; spell actions accept
only explicit learned enum indexes and enforce pin state. Resync requests are
deduplicated and contain no gameplay arguments. The native component cannot
emit generic item use/drop, quest mutation/tracking, keyboard, mouse,
controller, SDL, or arbitrary GML commands.

The bridge epoch is the per-session nonce. Each accepted local request also
receives a process-monotonic request ID, while each emitted JSON envelope gets
a strictly increasing command sequence. The initial sequence is a JSON-exact
time/process seed and can be raised by `command_sequence_floor` from a
previously validated acknowledgement. Only protocol-defined fields are placed
in `command.json`; internal request IDs and producer nonces are not serialized.

## Safety and flow control

- The queue is bounded to 1-64 requests (16 by default).
- Writes are rate-limited to 25-1000 ms (75 ms by default).
- Exactly one command can await acknowledgement at a time.
- The acknowledgement must match protocol, schema, epoch, sequence, and action.
- Missing acknowledgement fails the session closed after the configured
  250-30000 ms timeout (3000 ms by default); it does not retry a mutation.
- An epoch change, invalid state update, stale state regression, or explicit
  clear discards queued work.
- Topic revision changes between queuing and emission drop the stale request.
- An existing `command.json` is never replaced.

The writer creates a unique temporary file in the bridge directory with
`CREATE_NEW`, writes and flushes the complete JSON, closes it, and uses a
same-directory `MoveFileExW(..., MOVEFILE_WRITE_THROUGH)` without
`MOVEFILE_REPLACE_EXISTING`. Failed writes remove only that uniquely owned
temporary file.

## Native integration

`Panel.cpp` calls `UpdateSession` only after `LiveStateIngestor` independently
validates the full/delta chain described in `GML_BRIDGE_PROTOCOL.md`. It
populates inventory, armor, hotbar, and magic revisions; published sizes;
selected-hotbar index; explicit learned spell indexes; pinned index; and the
eight capability booleans from that accepted snapshot.

After `Pump` returns `kWritten`, the state/ack reader must pass the parsed
acknowledgement to `HandleAcknowledgement`. Both successful and rejected game
acks complete that one request; the caller remains responsible for displaying
the ack result code and refreshing live state. I/O and timeout results should
use the existing rate-limited native logger rather than logging each frame.

## Proof and remaining unknowns

`tests/ActionTransportTests.cpp` covers fail-closed session validation, UTF-8
and JSON escaping, capability and index validation, stale revision rejection,
bounded queues, atomic output shape, no-overwrite behavior, monotonic IDs and
sequences, rate limiting, exact acknowledgement matching, timeouts, and epoch
transitions. `build.ps1` compiles and runs this test using the proxy's strict
`-Wall -Wextra -Werror` settings.

Still unproven in the running game: discovery of the host-visible
`CONFIG_DIRECTORY`, Wine/FEX rename visibility and timing, GML acknowledgement
latency, animation/state transition behavior, and end-to-end mutation. The
panel/state integration is complete; these now require an explicit live test.

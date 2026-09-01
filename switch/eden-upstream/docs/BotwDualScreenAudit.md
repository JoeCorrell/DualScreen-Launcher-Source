# Breath of the Wild dual-screen technical audit

## Supported target

- Title ID: `01007EF00011E000`
- The native bridge is enabled only when the running executable build ID exactly matches the audited build ID in `botw_companion.cpp`.
- All guest addresses in this document are offsets in that executable. They must not be reused for another update without a new disassembly and validation pass.

## Native action comparison

| System | Original game path | Previous companion path | Current disposition |
| --- | --- | --- | --- |
| Equipment | The pause-menu action resolves a pouch entry, mutates the equipped marker through `AutoEquip` or `QuickEquip`, submits `RequestArmor` or `RequestEquippedItem`, waits for the equipment actor slot to finish loading, and then requests the native player refresh. | A regression published success immediately after the pouch mutation, bypassing the actor-ready and player-refresh half of the native path. The bridge again keeps equipment requests staged: it validates the target marker and actor-slot state, waits through native loading, calls the game's readiness check at `0xB249D4`, resolves the current player, and sets the same refresh bit used by the native equipment apply path. | Enabled for weapons, bows, shields, head, chest, and legs. Success is not published until native equipment loading and player refresh dispatch complete. |
| Unequip | Vanilla calls `PauseMenuDataMgr::unequip` at `0xD9A590`, resolves the slot's native default equipment with `0xD940C4`, and calls `PlayerBase::switchEquipment` at `0xCBE4D0`. | The companion validates the exact equipped pouch entry and live player before mutation, releases the pouch lock, invokes those three native operations in the same order, and verifies that the target's equipped marker was cleared. | Enabled for weapons, bows, shields, head, chest, and legs. |
| Runes | The native rune manager owns the selected rune and applies its normal state transition. | A mailbox request selects a validated, unlocked rune through the native rune manager. | Enabled. |
| Fast travel | Native travel requires a discovered destination and valid map and position identifiers. | A mailbox request dispatches the native travel function after the host validates the shrine state and exact identifiers. | Enabled for entered or cleared shrines. |
| Eat | Native setup enters the shared gameplay body at `0xE3EDE4`, applies the item's full health, stamina, and status logic, decrements exactly one item through `0xD97B84`, finalizes the selected item's native string, and dispatches its player event, animation, and sound through `0xD4279C`. | The broken path depended on fields owned by an open pause screen and called only part of the consume sequence. The bridge now builds the exact `0x80` native frame, supplies the real tab and slot, current player, persistent screen registry, pouch manager, and selected pouch item, enters at `0xE3EDE4`, exits immediately after the native decrement, then reproduces the vanilla caller callback and `0xD4279C` dispatch. It never reads the active-inventory menu state. | Enabled for materials and prepared food/elixirs while the persistent registry, player, pouch, and notifier are valid. Completion requires the matching mailbox payload and an authoritative quantity decrease of exactly one. |
| Hold | The lower routine at `0xD9F0E8` updates the pouch, records the grabbed item, and creates a CarryBox, but it does not attach the result to a moving player. The original higher-level action at `0xDA46A0` also resolves the player and sends the carry-state message. | The previous companion called `0xD9F0E8` directly, which left spawned objects at their world position. The host now stages the validated live pouch item in the native Hold action controller and the bridge invokes the complete `0xDA46A0` action, accepting success only when its native output flag is set. | Enabled for materials when the native action controller and its gate are valid. The request succeeds only after guest dispatch and authoritative pouch and grabbed-item confirmation. |
| Drop | The original flow around `0xE40BF0` uses the native discard and held-item callbacks. | No complete native companion dispatch exists. | Not exposed. |

Drop remains unavailable until its native action state and callbacks are mapped. Visual preview updates are never treated as evidence that a gameplay action succeeded.

## Mailbox invariants

The mailbox is a single-producer/single-claim command block. Its stable header is:

| Offset | Field |
| --- | --- |
| `0x00` | request payload |
| `0x08` | completed payload |
| `0x10` | state |
| `0x14` | item type or command sentinel |
| `0x18` | Eat tab / command metadata |
| `0x1C` | 32-bit bounded attempt count |
| `0x20` | diagnostic item slot for Eat |
| `0x24` | diagnostic action stage |
| `0xC8` | validated native Hold controller |
| `0x3628` | synthetic Eat screen's pouch-manager field |
| `0x3648` | synthetic Eat screen's selected-item field |

The host writes payload fields first and release-publishes `Queued`. The game-thread hook acquire-loads the state and atomically claims it as `Claimed`. Immediate native actions publish a terminal result only after their native call chain returns. Equipment transitions to `Running`; each later validation pass atomically claims it as `Checking`, then either releases it back to `Running`, rejects with a specific stage, or publishes success after the native player refresh request. The attempt counter is a bounded 32-bit field rather than a wrapping byte.

Mailbox metadata is preserved at `sp+0x48`, outside the action context and outside the saved target and return-address slots. It is loaded into `w17` so the inventory-list sentinel in `x14` remains valid for the native equip call. The mailbox occupies RW pages and the generated action bridge occupies a separate page-aligned RX segment; their ranges are asserted not to overlap.

## Snapshot and lifecycle audit

- Pouch traversal validates list structure before and after a read and retries once when the game mutates the list concurrently.
- Quest output includes native state `1` only. Completed quests are not mislabeled as active.
- Lightweight polling reports action state and attempts, allowing the UI to wait for authoritative completion without forcing a full inventory scan.
- A transient missing player or module reports loading without clearing process-keyed caches.
- The presentation requires six consecutive missing-title samples before replacing an active companion with Waiting for Game. Switching directly to another supported title remains immediate.
- ROMFS reads capture the current title and root while holding the emulator lock, then release it before archive reads, decompression, decoding, or PNG creation.

## Performance audit

- Full inventory, quest, shrine, and equipment snapshots are event-driven or slow-poll data. Lightweight health, stamina, position, action, and selected-rune data use the frequent poll.
- Runtime textures are requested once, decoded off the UI thread, cached, and retried with a bounded delay after failure.
- The composite map bitmap is held separately from the 24 MB icon LRU. Its decoded size exceeds that LRU on common devices; placing it in the LRU caused immediate eviction, repeated JNI/ROMFS reads and decodes, and severe frame loss.
- Successful native map generation is cached for the current ROMFS session.
- No model, texture, shader, inventory list, or map should be recreated during a normal draw call.
- Visible inventory rows and category totals are cached until the inventory source or selection changes, avoiding repeated list construction and category-wide scans during draw.

## Asset and rendering findings

- Bundled Nintendo model and texture copies have been removed from the APK. UI textures resolve from the running game's ROMFS and are stored only in the app's runtime cache.
- Cooked elixirs use effect-specific ROMFS stock icons. The companion reads the native CookData effect ID at pouch-item offset `0x8C` and resolves `Item_Cook_C_17_<effect>` rather than treating every elixir as the generic `Item_Cook_C_17` actor.
- The current character preview is a separate Filament GLB renderer. It is not BOTW's native pause-menu renderer and cannot reproduce BFRES material graphs, dye parameters, body masks, skeleton variants, or every equipment attachment solely from an actor-to-GLB manifest.
- The manifest covers the known armor and equipment actor names, but exact manifest coverage does not guarantee correct materials or body assembly.
- A complete armor fix requires either a native second-view invocation of BOTW's character renderer or a runtime BFRES-to-renderer pipeline that preserves models, skeletons, materials, textures, dyes, body masks, LOD selection, and per-item attachment metadata. Missing runtime models are reported; the UI must not substitute unrelated armor.
- The map uses ROMFS terrain and marker textures, preserves original marker aspect ratios, and uses the native player marker texture. It is still a companion map composition, not a second invocation of BOTW's native map renderer.
- Memory and Goddess Statue reference markers are intentionally omitted; gameplay travel landmarks remain visible.
- Dynamic quest objectives, custom stamps, undiscovered marker states, and atlas pane UV definitions require additional native data hooks before they can be claimed as exact.

## Regression matrix

Before enabling a new native action or supporting another executable build, test all of the following with action and lifecycle logging enabled:

1. Load an existing save and a new save.
2. Equip each weapon class, shield, bow, and mixed armor set.
3. Select every unlocked rune and verify the live player state, not only the companion highlight.
4. Eat one material and one cooked meal, then hold one through five materials; verify the real player action, effects, pouch quantities, carried actors, dropping, movement, and preview confirmation.
5. Change inventory while the companion list is being polled.
6. Walk across streaming boundaries, open and close pause menus, enter and leave shrines, and start combat.
7. Fast travel, die and respawn, reload a save, return to the title screen, suspend, and resume.
8. Repeat title switching between supported games and verify there is one presentation and one polling worker.
9. Record top-screen frame time, bottom-screen draw time, poll time, RAM, and GPU memory for at least 30 minutes.
10. Reject the build if requests remain Running, memory grows continuously, textures decode repeatedly, the presentation falls back to Waiting during ordinary play, or any guest exception is introduced.

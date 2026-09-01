# Fallout 3 1.7.0.4 binary interoperability notes

Date: 2026-08-29

This document records static, interoperability-focused analysis of the exact executable installed on the AYN Thor. It is not a DRM-bypass or redistribution guide. No game file was modified. The only proprietary files examined locally were the executable and small configuration/metadata files already owned by the user.

## Scope and evidence rules

Three evidence levels are used below:

- **High**: proved directly in the installed 1.7.0.4 binary, usually through two or more independent call sites or a command descriptor plus its disassembly.
- **Medium**: structurally supported by the current binary and corroborated by the 1.7.0.3 FOSE layouts, but still needs a runtime canary before dereferencing or invoking it.
- **Reference only**: known for 1.7.0.3 and useful for naming/semantics, but not safe to use against the installed binary yet.

Reference source was used only to name structures and compare semantics. It must not be copied wholesale into the project, and its addresses must never be used for this executable.

## Exact target

| Property | Installed executable |
|---|---|
| Path on device | `/storage/emulated/0/Download/Fallout-3-GOTY-SteamRIP.com/Fallout 3/Fallout3.exe` |
| Size | 16,855,040 bytes |
| SHA-256 | `c3f97c2255fa041a851c17cf372d69aaadd8694e2dc4230ba556001bbfbd2f3e` |
| File/product version | 1.7.0.4 |
| PE architecture | PE32 / i386 |
| Preferred image base | `0x00400000` |
| Link timestamp | `0x60A80E56` = 2021-05-21 19:47:34 UTC (15:47:34 EDT) |
| Entry point VA / RVA | `0x00D7A565` / `0x0097A565` |
| Relocations / ASLR | Present; `DllCharacteristics=0x8140` |
| Signature | No Authenticode signature in this file |

Relevant sections are:

| Section | VA | raw offset | virtual/raw size used in mapping |
|---|---:|---:|---:|
| `.text` | `0x00401000` | `0x00000400` | `0x00B2297C` |
| `.rdata` | `0x00F24000` | `0x00B22E00` | `0x001D4AA0` |
| `.data` | `0x010F9000` | `0x00CF7A00` | `0x00126A00` |

The game imports DirectInput 8 and XInput 1.3, plus D3D9/D3DX9-era functions (`D3DPERF_SetOptions` and D3DX9_42 texture/shader/matrix helpers). It imports the normal USER32 window/input APIs. This supports a proxy/bootstrap route, but a D3D9 proxy must forward the exact export set expected by the process/DXVK and must not initialize the mod while loader lock is held.

The installed directory also contains a separate, older `1.7.0.3` executable:

| Property | Bundled older copy |
|---|---|
| Relative location | `_Downgrader to v1.7.0.3/Fallout3.exe` |
| Size | 15,044,024 bytes |
| SHA-256 | `ae058c497069b8334711945cf646968b7fe8da483cce94022fc924fa0187e809` |
| Version | 1.7.0.3 |
| Timestamp | 2009-06-23 |

The active binary is **not** an offset-shifted 1.7.0.3 image. Normalized byte-pattern comparisons of known 1.7.0.3 functions did not survive, and section/link characteristics differ substantially. It is a recompilation/relink. Do not apply FOSE 1.7.0.3 addresses or a fixed address delta. Every current address below is valid only after matching the exact SHA-256 and still needs ASLR rebasing (`moduleBase + RVA`).

## Discovery strategy that survives ASLR

For the exact hash, RVAs are acceptable as a fast path. A production build should also perform structural validation and fail closed:

1. Verify PE machine is `IMAGE_FILE_MACHINE_I386` and version/hash is supported.
2. Locate the vanilla `CommandInfo` arrays from command names and validate the complete descriptor (name, opcode, parameter count, handler pointer).
3. Decode global pointers and native callees from validated handlers rather than hardcoding their absolute VAs.
4. Locate polymorphic types through MSVC RTTI type descriptors (`.?AVName@@`), complete-object locators, and vtables.
5. Before enabling reads, check pointer alignment, committed/readable pages, expected vtable range in `.rdata`, form type, and a small set of invariants.
6. Before enabling writes/actions, require the game-thread executor, a current player generation, and exact handler signature validation.

The current vanilla `CommandInfo` structure is still 0x28 bytes:

```text
+00 const char* longName
+04 const char* shortName
+08 uint16 opcode
+0A padding
+0C const char* helpText
+10 uint16 needsParent
+12 uint16 numParams
+14 ParamInfo* params
+18 execute
+1C parse
+20 eval
+24 flags
```

The two current table bases are:

| Table | VA / RVA | Evidence |
|---|---:|---|
| Script commands | `0x0110B388` / `0x00D0B388` | `SetStage` opcode `0x1039`, `EquipItem` opcode `0x10EE`, and other descriptors all calculate the same base. **High** |
| Console commands | `0x01110C70` / `0x00D10C70` | `TestLocalMap` opcode `0x0157` and `OutputLocalMapPictures` opcode `0x0182` calculate the same base. **High** |

The 1.7.0.3 count of 568 script commands and 204 console commands would make the end pointers `0x01110C48` and `0x01112C50`; the current starts are high confidence, while these end/count assumptions are **Medium** until every entry is structurally checked at runtime.

## Current 1.7.0.4 landmarks

All values are unrebased static VAs followed by RVAs. Never use the VA directly in an ASLR-enabled process.

### Globals

| Meaning | VA / RVA | Evidence |
|---|---:|---|
| `PlayerCharacter**` | `0x0123C674` / `0x00E3C674` | Used by current equip, unequip, actor-value, active-quest, radiation, and radio paths. **High** |
| `DataHandler**` | `0x012249E4` / `0x00E249E4` | Current `CompleteAllObjectives` walks `[global]+0xD4` as the quest list. **High** |
| `InterfaceManager**` | `0x01239CA4` / `0x00E39CA4` | Decoded from the current item/Pip-Boy refresh implementation and cross-version semantic match. **High** |
| selected radio entry | `0x012393D8` / `0x00E393D8` | Written by the current tune path; first field resolves to a station reference. **High** |
| radio enabled byte | `0x012393F0` / `0x00E393F0` | Read/written by current radio on/off and tune paths. **High** |
| radio transition guard | `0x012393F2` / `0x00E393F2` | Rejects tune while a radio transition is in progress. **High** |
| placed radio-source list | `0x01220EF4` / `0x00E20EF4` | Walked by the available-station builder. Exact ownership/lifecycle still needs runtime validation. **Medium** |
| actor-value metadata array | `0x0121FB08` / `0x00E1FB08` | Indexed by actor-value mutation handlers, then dereferenced for metadata flags. **High** |

### Core helpers and native actions

| Meaning | VA / RVA | Evidence / rule |
|---|---:|---|
| command argument extractor | `0x00787530` / `0x00387530` | Called by all examined vanilla handlers. **High**, but do not fabricate compiled-script argument buffers. |
| MSVC runtime dynamic cast helper | `0x00D7B79B` / `0x0097B79B` | Used by current item/action handlers. **High** |
| `Actor::EquipItemAlt`-equivalent | `0x007F0A30` / `0x003F0A30` | Direct native callee in `Cmd_EquipItem_Execute`. **High** |
| `Actor::UnequipItem`-equivalent | `0x007F1180` / `0x003F1180` | Direct native callee in `Cmd_UnequipItem_Execute`. **High** |
| quest objective resolver | `0x00773EB0` / `0x00373EB0` | Walks `TESQuest+0x4C`, compares `objective+0x04` with objective ID. **High** |
| quest stage setter | `0x00775840` / `0x00375840` | Called by `SetStage`; walks `TESQuest+0x44`, updates current stage at `+0x60`, evaluates stage data. **High** |
| active quest setter | `0x008C4380` / `0x004C4380` | Called by `ForceActiveQuest`; stores `TESQuest*` at `PlayerCharacter+0x614`. **High** |
| objective state mutator | `0x005AC710` / `0x001AC710` | Called by displayed/completed commands; performs notifications and active-quest behavior. **High** |
| UI message queue helper | `0x008FE390` / `0x004FE390` | Current equip handler uses it in the same semantic position as old UI queue code. **High** |
| vanilla item-list refresh | `0x008FEA30` / `0x004FEA30` | Current equip/unequip handler refresh path. This is an oracle/dirty signal, not something the custom UI should call on refresh. **High** |
| radio enable/disable | `0x008E52B0` / `0x004E52B0` | `PipBoyRadioOff` pushes false and calls this function. **High** |
| tune/select station | `0x008E6590` / `0x004E6590` | `PipboyRadio` calls it with a reference and boolean. **High** |
| refresh available radio state | `0x008E6750` / `0x004E6750` | `ForceRadioStationUpdate` pushes true and calls this function. **High** |
| build available-station candidates | `0x005B2990` / `0x001B2990` | Called by tune with player, output list, and zero flags when no explicit station is supplied. **High** |
| map-marker extra getter | `0x0075D3C0` / `0x0035D3C0` | Adds `0x40` to the reference (extra-data list) and resolves map-marker data. **High** |

Useful resilient on-disk patterns (wildcards are relocations/call displacements) are:

```text
Equip native:
55 8B EC 56 57 8B 7D 08 8B F1 0F B6 47 04 83 C0 E8 83 F8 1B 77 ??

Unequip native:
55 8B EC 56 8B F1 8B 46 60 85 C0 74 ?? 83 78 28 01 7F ?? 3B 35 ?? ?? ?? ??

DataHandler/quest-list use inside CompleteAllObjectives:
A1 ?? ?? ?? ?? 05 D4 00 00 00 74 ?? 53 57 83 78 04 00
```

These patterns are not sufficient by themselves. Validate call graph, return convention, containing section, and expected descriptor before use.

## Proven command descriptors

The following descriptors come directly from the current script table:

| Command | Descriptor VA | Opcode | Params | execute | eval |
|---|---:|---:|---:|---:|---:|
| `AddItem` | `0x0110B3D8` | `0x1002` | 3 | `0x0078DFF0` | 0 |
| `GetActorValue` | `0x0110B5B8` | `0x100E` | 1 | `0x00793080` | `0x006EE640` |
| `GetItemCount` | `0x0110BAE0` | `0x102F` | 1 | `0x00795190` | `0x006F20A0` |
| `SetStage` | `0x0110BC70` | `0x1039` | 2 | `0x007A6BA0` | 0 |
| `GetStage` | `0x0110BC98` | `0x103A` | 1 | `0x007962F0` | `0x006F4210` |
| `RemoveItem` | `0x0110C058` | `0x1052` | 3 | `0x007A05A0` | 0 |
| `EquipItem` | `0x0110D8B8` | `0x10EE` | 3 | `0x007921C0` | 0 |
| `UnequipItem` | `0x0110D8E0` | `0x10EF` | 3 | `0x007AF560` | 0 |
| `SetObjectiveCompleted` | `0x0110F4D8` | `0x11A2` | 3 | `0x007A59D0` | 0 |
| `SetObjectiveDisplayed` | `0x0110F500` | `0x11A3` | 3 | `0x007A5AB0` | 0 |
| `GetObjectiveCompleted` | `0x0110F528` | `0x11A4` | 2 | `0x00795540` | 0 |
| `GetObjectiveDisplayed` | `0x0110F550` | `0x11A5` | 2 | `0x007955F0` | 0 |
| `PipboyRadio` | `0x0110F5A0` | `0x11A7` | 2 | `0x0079DC60` | 0 |
| `ForceActiveQuest` | `0x0110FE10` | `0x11DD` | 1 | `0x00792720` | 0 |
| `GetMapMarkerVisible` | `0x01110068` | `0x11EC` | 0 | `0x00795490` | `0x006F2CA0` |
| `GetRadiationLevel` | `0x01110220` | `0x11F7` | 0 | `0x007960C0` | `0x006F3A80` |
| `ResetPipboyManager` | `0x011103B0` | `0x1201` | 0 | `0x007A1100` | 0 |
| `GetQuestCompleted` | `0x011108D8` | `0x1222` | 1 | `0x00796020` | `0x006F39A0` |
| `PipBoyRadioOff` | `0x01110928` | `0x1224` | 0 | `0x0079FFB0` | 0 |
| `ForceRadioStationUpdate` | `0x01110A68` | `0x122C` | 0 | `0x00792D90` | 0 |

Console descriptors:

| Command | Descriptor VA | Opcode | Params | execute |
|---|---:|---:|---:|---:|
| `TestLocalMap` | `0x01111A08` | `0x0157` | 1 | `0x007AB800` |
| `OutputLocalMapPictures` | `0x011120C0` | `0x0182` | 0 | `0x0079CDE0` |

The command descriptors are valuable semantic anchors. The custom mod should not invoke their `execute` functions by constructing a fake script context. Those handlers depend on the script VM argument decoder and several hidden preconditions. Instead, resolve and call the underlying native method on the game thread with a real object/stack, or use the command path only as an observation hook.

## Player statistics and actor values

The current `GetActorValue` implementation proves the following current-build layout:

- Actor-like references expose `ActorValueOwner` at `actor + 0x9C`.
- The current-value call is virtual slot `+0x0C` in the owner vtable.
- The base-value call is virtual slot `+0x00`.
- The implementation accepts actor-value IDs through `0x48` (72).
- It validates an actor through its form/reference vtable before accessing the owner.

The current enum range agrees with the reference build and should be verified against the current actor-value metadata table at startup. Important IDs are:

```text
5..11   Strength, Perception, Endurance, Charisma, Intelligence, Agility, Luck
12      ActionPoints
13      CarryWeight
16      Health
18      DamageResistance
20      RadiationResistance
23      Karma
24      XP
25..31  Head, Torso, LeftArm, RightArm, LeftLeg, RightLeg, Brain condition
32..45  Barter through Unarmed skills
46      InventoryWeight
54      RadiationLevel
```

Actionable reader:

1. On the game thread, obtain and validate `*PlayerCharacter**`.
2. Read current/base values through the owner vtable, not through fixed backing arrays.
3. Clamp actor-value ID to the current supported range and validate the metadata entry.
4. Snapshot primitive results only; never expose the engine pointer to the UI thread.

Level should use the ActorValueOwner/actor virtual used by the engine rather than a guessed player offset. HP, AP, XP, SPECIAL, skills, limb condition, carry weight, radiation, resistances, and karma can all be supplied by this path. Caps and ammunition are item counts, not actor values.

Active effects, perks, reputation, and the exact level getter remain runtime/static follow-ups; do not substitute hardcoded values.

## Inventory, equipment, and item actions

### Current read path

The current `GetItemCount`, `AddItem`, and `RemoveItem` implementations confirm that container contents are not a flat item array. `TESObjectREFR+0x40` is its extra-data list; the engine resolves container changes and then operates on an entry and, where applicable, a specific stack.

The current `0x005D2660` helper independently proves that `ExtraContainerChanges` is extra-data type `0x15`: it calls the generic list lookup with immediate `0x15` and returns the payload pointer from `extra+0x0C`. This helper is a safe read-only root lookup when called with `ECX=&reference->extraDataList`; unlike `0x00622290`, it does not create missing container state. Do **not** use `0x00622290` in the read-only sampler because that higher helper allocates/attaches container-change data when absent.

The following layout is strongly corroborated by both the current handlers and the reference structures:

```text
TESForm
  +04 uint8 formType
  +08 flags
  +0C uint32 FormID

TESObjectREFR
  +1C TESForm* baseForm
  +20 rotation
  +2C position
  +3C TESObjectCELL* parentCell
  +40 ExtraDataList

ExtraContainerChanges data
  +00 tList<EntryData>* objList
  +04 TESObjectREFR* owner
  +08 float totalWeightCurrent
  +0C float totalWeightLast

EntryData
  +00 tList<ExtraDataList>* extendData
  +04 int32 countDelta
  +08 TESForm* type
```

The exact `ExtraContainerChanges` object header should still receive runtime canaries. The current removal handler directly accesses reference extra data at `+0x40`, and the dedicated current helper proves type `0x15`, so the root path itself is **High** confidence.

Equipment state must be determined per stack:

- `ExtraWorn` identifies the normal/right-hand worn stack.
- `ExtraWornLeft` identifies the left-hand worn stack.
- `ExtraCount` contributes a stack-specific count.
- `ExtraHealth` is stack condition; condition percentage is based on stack health divided by the base form's maximum-health component.

Do not mark a base item equipped merely because another stack with the same FormID is equipped. Duplicate weapons can have different health/mod state. A UI selection token should therefore include save-generation, FormID, hand, and a stable stack fingerprint resolved again on the game thread.

### Current equip and unequip calls

`Cmd_EquipItem_Execute` parses the form/count/flags, casts the command subject to `Actor`, pushes six native arguments, and calls `0x007F0A30`. It then compares the actor against `*0x0123C674` and triggers vanilla UI notifications/refreshes.

The call site at `0x00792307` is:

```text
6A 01
FF 75 E4
8B CB
6A 01
6A 00
6A 01
FF 75 EC
E8 ?? ?? ?? ??       ; 0x007F0A30
3B 1D 74 C6 23 01   ; compare with current player
```

`Cmd_UnequipItem_Execute` performs the corresponding call to `0x007F1180`; the call site at `0x007AF660` has the same six-argument stack shape. Both native functions return with `ret 0x18`.

The precise names/semantics of all six parameters need a runtime action matrix. The old build's prototypes and the current call sites agree on form, count, stack extra data, and three boolean/control arguments, but guessing the boolean labels would be unsafe.

Recommended action path:

1. Touch/controller UI enqueues `Equip`, `Unequip`, or `Use` with the immutable selection token.
2. A validated game-thread tick resolves the current player and current container generation.
3. It resolves the exact current `EntryData` and stack `ExtraDataList`.
4. It calls the native equip/unequip function once.
5. It does not optimistically mutate the snapshot and does not call the vanilla Pip-Boy refresh function.
6. The equipment/container dirty hook observes the actual state change; the next snapshot confirms the action to the UI.

This avoids double-press behavior, duplicate equip events, use-after-load pointers, and UI refresh feedback loops. Aid use should follow the engine's actual item-use/equip branch after the native parameter semantics are verified; do not implement consumption by decrementing inventory manually.

### Item metadata

#### Proven player-facing name path

The exact 1.7.0.4 binary contains a uniform inventory-entry name helper at VA/RVA `0x00621EF0` / `0x00221EF0`:

```text
const char* __thiscall EntryData_GetDisplayName(EntryData* this)
```

It reads `this+0x08`, RTTI-casts the `TESBoundObject` to `TESFullName`, checks the `BSString` length, and returns the loaded/localized display-name pointer. It returns the empty string at `0x00F3C684` when the component exists but its data pointer is null, and the string referenced by `0x011018FC` (`<Missing Name>` in this image) when the FullName component/name is unavailable. This is preferable to a table of form-type-specific name offsets. It is also what the current `InventoryChanges.cpp` code uses, so it covers weapons, apparel, aid, ammo, and miscellaneous bound objects through the same path.

Production signature, unique once in this exact executable (relocated RTTI/call operands are wildcarded):

```text
pattern:
6A 00 68 00 00 00 00 68 00 00 00 00 6A 00 FF 71 08 E8 00 00 00 00 8B D0 83 C4 14 85 D2 74 35 0F B7 42 08 B9 FF FF 00 00 66 3B C1 75 13 8B 4A 04
mask:
xxx????x????xxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxx
```

Required structural canaries:

- The match is in `.text`; its two pushed RTTI operands resolve to current `TESFullName` TD `0x01203468` and `TESBoundObject` TD `0x0120569C`, and the decoded call is the current MSVC cast helper `0x00D7B79B`.
- `EntryData+0x08` already passed form/vtable/type/FormID validation. Never call this with a fabricated entry or a raw `TESForm*` in `ECX`.
- A non-fallback result must be committed readable memory, terminate within 4,096 bytes, and agree with the component's 16-bit string length (`TESFullName+0x08`; `0xFFFF` means the helper itself measures the NUL-terminated string).
- Copy the bytes during the game-thread snapshot. Do not retain the engine string pointer across a load, language change, or form unload. Convert the copied game encoding for the lower-screen transport afterward.
- Treat `<Missing Name>`, an empty name, malformed length, or a failed pointer canary as an unavailable field, not as player-facing text.

**Gate:** High confidence and suitable for the first read-only deployment.

#### Proven vanilla InventoryMenu category mapping

The current InventoryMenu filter is at VA/RVA `0x0093A7C0` / `0x0053A7C0`; its exact category branch begins at `0x0093A833`. The function consumes an `EntryData*`, reads `entry+0x08` form type, and returns `AL=true` for an entry that should be filtered out. It depends on the live vanilla menu singleton/category at `menu+0x60`, so the custom always-on UI should reproduce the proven mapping rather than call this function while the vanilla menu is closed.

| Vanilla tab index | Player-facing category | Accepted current form types |
|---:|---|---|
| 0 | Weapons | `0x28` Weapon |
| 1 | Apparel | `0x18` Armor, `0x1A` Clothing |
| 2 | Aid | `0x1D` Ingredient, `0x2F` AlchemyItem, `0x19` Book |
| 3 | Misc | remaining accepted inventory forms after excluding Weapon, Armor, Clothing, Ingredient, AlchemyItem, Ammo, and Book |
| 4 | Ammo | `0x29` Ammo |

Type `0x2E` Key is explicitly filtered before the tab switch because vanilla handles keys through its keyring path. Notes (`0x31`) and ordinary miscellaneous objects (`0x1F`) reach Misc if they pass the earlier generic playability/visibility checks. The meanings of helpers `0x006EBCD0`, `0x006EBE20`, and `0x006F8F60` in those earlier checks have not been assigned without proof; do not label or bypass them as a supposed `playable` flag yet.

Unique core-branch signature:

```text
pattern:
80 FB 2E 74 21 83 FE 04 77 1C FF 24 B5 00 00 00 00 5F 80 FB 28 5E 0F 95 C0 5B 5D C3 80 FB 18 74 3E 80 FB 1A 74 39 5F 5E B0 01 5B 5D C3 80 FB 1D 74 2D 80 FB 2F 74 28 80 FB 19 EB E8 80 FB 28 74
mask:
xxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

Validate that the wildcard jump-table operand contains exactly five `.text` targets corresponding to the branches above. **Gate:** category mapping is High confidence for read-only use; calling the menu-dependent filter is not required or recommended.

#### Exact container and list canaries

Before resolving any metadata, a snapshot must pass all of these checks:

- `0x005D2660` is found by its already documented type-`0x15` signature and returns null or a readable payload. Null is a valid loading/empty state.
- Payload `+0x04` is null or the validated player owner. Its `+0x00` entry-list pointer is null or readable/aligned.
- Every entry node and `EntryData` is readable/aligned; `entry+0x08` points to a form whose primary vtable lies in `.rdata`, whose type is within the current form enum, and whose FormID is plausible. `entry+0x04` is signed and bounded before it participates in a total.
- Traverse no more than 8,192 entry nodes, reject cycles, and reject a list that changes head/next pointers during the same game-thread sample. Re-read the root/head at the end and discard the entire snapshot if they differ.
- Each `extendData` node and `ExtraDataList*` receives independent readability, alignment, 8,192-node, and cycle checks. An extra list has its object head at `+0x04` and a presence bitfield beginning at `+0x08`; never follow a type bit without validating the matching object in the linked list.
- Proved current extra type IDs are ContainerChanges `0x15` and Health `0x25`. Worn `0x16`, WornLeft `0x17`, and Count `0x24` still require live stack canaries before their values are exposed, even though the older reference layout agrees.
- Snapshot only primitive copies. Any failed entry/list/stack predicate aborts that entry or snapshot; it must never lead to a blind retry, write, or get-or-create call.

#### Exact read helpers with field-level gates

These helpers are independently present in the exact 1.7.0.4 binary and do not mutate the form. Their broad semantic role is proven, but only the name/category paths above are mandatory for the first useful inventory snapshot.

| Field | VA / RVA | Calling convention and exact result | Deployment gate |
|---|---:|---|---|
| base gold value | `0x007B9FA0` / `0x003B9FA0` | `int32 __cdecl(TESForm*)`; returns `TESValueForm+4`, calculated magic/Alchemy value where applicable, or `-1` unsupported | Field-gated. This is base value, not condition/barter-adjusted sale value. |
| weight | `0x007C7820` / `0x003C7820` | `float(ST0) __cdecl(TESForm*)`; current type switch returns the appropriate `TESWeightForm` value or `-1.0f` unsupported | Field-gated until finite/range and known-item canaries pass. |
| base attack damage | `0x006EB270` / `0x002EB270` | `uint16/EAX __cdecl(TESForm*)`; RTTI-casts to `TESAttackDamageForm` and invokes component vslot `+0x10`, otherwise zero | Field-gated and label **base damage**, not Pip-Boy skill/condition-adjusted damage. |
| health component | `0x00714F00` / `0x00314F00` | `TESHealthForm* __cdecl(TESForm*)`; supports Armor `+0x68`, Weapon `+0x94`, NPC/Creature `+0xB0`, else null | High confidence as a component locator. |
| entry condition | `0x00622C80` / `0x00222C80` | `float(ST0) __thiscall(EntryData*, bool percent)`; uses first real stack ExtraHealth when present, otherwise base health/100% | Field-gated; do not fabricate entries to query later stacks. |

Relocatable unique signatures for the three scalar helpers:

```text
base value pattern:
55 8B EC 51 56 8B 75 08 6A 00 68 00 00 00 00 68 00 00 00 00 6A 00 56 E8 00 00 00 00 83 C4 14 85 C0 74 08 8B 40 04 5E 8B E5 5D C3
mask:
xxxxxxxxxxx????x????xxxx????xxxxxxxxxxxxxxx

weight pattern:
55 8B EC 8B 4D 08 85 C9 74 4B 0F B6 41 04 83 C0 E8 83 F8 1A 77 3F 0F B6 80 00 00 00 00 FF 24 85 80 00 00 00 00
mask:
xxxxxxxxxxxxxxxxxxxxxxxxx????xxxx????

base-damage pattern:
55 8B EC 6A 00 68 00 00 00 00 68 00 00 00 00 6A 00 FF 75 08 E8 00 00 00 00 83 C4 14 85 C0 74 08 8B 10 8B C8 5D FF 62 10 33 C0 5D
mask:
xxxxxx????x????xxxxxx????xxxxxxxxxxxxxxxxxx
```

Value canaries: result is `-1` or `0..10,000,000`; compare several loaded records against their `TESValueForm+4`; do not present it as merchant price. Weight canaries: finite `-1.0f` or `0..100,000`, with Ammo expected to be unsupported/weightless in the current switch. Damage canaries: only Weapon `0x28` should produce a meaningful nonzero base damage; zero remains valid for unusual weapons. Any mismatch disables only that field.

The weapon load path at `0x00773057` independently proves `BGSAmmoForm` begins at weapon `+0xA4`, so its referenced ammo/list form is at weapon `+0xA8`; `0x00589260` accepts only form type `0x29` Ammo or `0x55` ListForm. Reading `+0xA8` is therefore field-gated on primary `TESObjectWEAP` RTTI, type `0x28`, and a pointed-form type of `0x29` or `0x55`. Do not traverse a `0x55` list until its current list layout has its own bounds/cycle proof.

No single non-allocating inventory generation/event source was proved in this pass. `BaseExtraList::GetByType` at `0x005D2860` does not create engine extras, but it writes a thread-local lookup cache and takes a cache lock, so it should not be advertised as a pure dirty signal. For the first read-only deployment, use a low-frequency, allocation-free fingerprint over the already bounded entry/stack snapshot (FormID, signed count, list topology, presence bitfield, and proved ExtraHealth value), then publish only when that fingerprint changes. Equip/unequip hooks can later set an atomic dirty bit, but they are not a complete inventory-change source by themselves.

## Quest and objective state

The current binary independently proves the important 1.7.0.4 quest layout:

```text
TESQuest
  +30 TESFullName title
  +3C uint8 questFlags
  +3D uint8 priority
  +40 float delay
  +44 tList<StageInfo> stages
  +4C tList<BGSQuestObjective> objectives
  +54 conditions
  +5C ScriptEventList*
  +60 uint8 current/highest stage used by native stage code
  +64 BSString editorName

StageInfo
  +00 uint8 stage
  +01 uint8 isDone
  +04 tList<QuestStageItem> logEntries

BGSQuestObjective
  +00 vtable
  +04 uint32 objectiveId
  +08 BSString localized/display text
  +10 TESQuest* owningQuest
  +20 uint32 stateBits
```

Evidence:

- `DataHandler** 0x012249E4` plus `0xD4` is walked as a quest list by the current complete-objectives implementation.
- `0x00773EB0` walks `quest+0x4C` and compares `objective+0x04` with the requested objective ID.
- `0x00775840` walks `quest+0x44`, compares the first byte of each stage, updates `quest+0x60`, and runs stage data.
- Current objective getters read `objective+0x20`.
- Current objective setters call `0x005AC710`, preserving bit 0 while setting bit 1 for completion. Static behavior establishes bit 0 as displayed and bit 1 as completed.
- `0x008C4380` stores the active quest at `PlayerCharacter+0x614`; clearing it also clears related state starting at `+0x620`.

Quest reader rules:

- Enumerate the current `DataHandler+0xD4` quest list under a game-thread snapshot.
- Use `TESFullName`, not `editorName`, as the player-facing title.
- Use `BGSQuestObjective.displayText`, not script/editor strings, for objective text.
- Only show objectives with displayed bit set; mark completed only from completed bit.
- Use quest flags/current-stage behavior established from current functions. Validate exact flag meanings at runtime before filtering completed/stopped quests.
- Copy strings while the object is valid and bound their length; never pass `BSString` pointers to the UI thread.

Quest tracking action can call the current active-quest setter on the game thread after resolving a live quest pointer. Hook that setter and the stage/objective mutators only to set dirty bits; never serialize or rebuild quest UI inside the detour.

## Map and markers

Fallout 3's Pip-Boy map path is an engine map subsystem, not a gameplay-camera capture. The current binary contains independent `MapMenu` logic, `TestLocalMap`, `OutputLocalMapPictures`, local/world map zoom settings, map marker templates, local-map output paths, and world/local quest marker templates.

The marker visibility command proves:

1. A reference's marker extra is resolved from `TESObjectREFR+0x40` by current helper `0x0075D3C0`. Its tail-call target uses immediate type `0x2C`, proving the current `ExtraMapMarker` type.
2. The marker object's flag byte is at `marker+0x0C`.
3. `0x00635070` returns bit 0. If set, `GetMapMarkerVisible` returns 1.
4. `0x00635060` returns bit 1. If both are set, the command returns 2.
5. `0x00635050` returns a 16-bit field at `marker+0x0E`, likely marker type/icon metadata; label it only after runtime correlation.

Bit 0 is therefore visible. Bit 1 is a stronger discovered/fast-travel state, but its exact player-facing name needs runtime correlation against one visible-undiscovered and one discovered marker.

The native `MapMenu` vtable for the current build is:

| Slot role | Current function |
|---|---:|
| destructor | `0x00945100` |
| SetTile / construction | `0x00946A00` |
| left press | `0x00948D20` |
| click | `0x00947C10` |
| mouseover | `0x00948D60` |
| unmouseover | `0x00949AC0` |
| wheel | `0x00949B90` |
| update | `0x00949670` |
| GetID | `0x0094A3A0` |
| special key | `0x009491A0` |
| controller | `0x005A7770` |
| controller connect | `0x00948CE0` |

Use these functions as static/runtime oracles for coordinate normalization, zoom limits, marker construction, and discovery filtering. Do **not** instantiate/open a hidden `MapMenu`, switch the main render target, or feed its input handlers. The custom lower map consumes copied map data and maintains independent pan/zoom state. World player location comes from the validated player reference position/cell/worldspace. `OutputLocalMapPictures` performs file output and is not an appropriate runtime path.

### Confirmed Local Map coordinate domain

The exact 1.7.0.4 `MapMenu` local-map setup at `0x0094F140` proves the complete raster domain:

- It obtains the player point by calling the player reference vtable at `+0x1F4`; it does **not** read `PlayerCharacter+0x2C` directly.
- It uses `TESObjectCELL+0x3C` and the interior predicate before applying `0x0073F200(cell, nativePoint, mappedPoint, true)`.
- The grid-side global at `0x010FF880` is `5`, so one frame covers a 5x5-cell domain.
- Exterior base cells are `round(position) >> 12` minus half the grid side.
- Interior base cells subtract the native `2048`-unit half-cell origin before the same 4096-unit cell conversion.
- Published bounds use those same base cells and offsets. Local player, door and quest points must all pass through the virtual reference-position accessor and, for interiors, the same transform before projection.

Mixing raw `PlayerCharacter+0x2C` coordinates with virtual-position reference markers was therefore invalid even when both values happened to agree outdoors. The lower map now snapshots the virtual player point, raster bounds and all markers in one native domain. Pan/zoom only changes the immutable map projection; fixed panel chrome is rendered after the map-content DC is restored.

The installed `map_menu.xml` independently defines the native pan relationship:
`MM_LocalMap_ParentImage` alone consumes `dragdeltax/dragdeltay`; the cursor
computes its screen position as its normalized map coordinate multiplied by
the parent image size plus the parent's panned X/Y. Local-map marker templates
use the same parent dimensions. Raster, player cursor and pins must therefore
move as one map-space layer during a drag, while tabs, frames, footer and zoom
controls remain fixed. This is the invariant covered by the projection tests;
pinning the player cursor to the panel would diverge from Fallout's real map.

## Radio

The current radio control path is sufficiently concrete for a first interactive implementation:

- `0x008E52B0(bool)` enables/disables radio. The vanilla `PipBoyRadioOff` handler calls it with false.
- `0x008E6590(TESObjectREFR*, bool)` tunes/selects a station. It checks enabled byte `0x012393F0` and transition guard `0x012393F2`.
- When no explicit reference is supplied, it calls `0x005B2990(*player, outputList, 0)` to build available candidates.
- Candidate station entries expose a station reference at `+0x1C` in the temporary candidate object used by this path.
- The resolved/current radio entry is stored at `0x012393D8`; the selected entry's first field is the station reference in the engine's persistent radio-entry shape.
- `0x008E6750(true)` forces station-state reevaluation and checks player cell/world state.

The lower UI can snapshot the available-candidate list on a low-frequency/cell-change dirty event, copy station name/ref ID and availability, then free only the temporary list using the exact owner routine. It must not retain temporary candidate nodes. Tune/off commands must execute on the game thread through the native functions after verifying the station is still available.

The installed `menus\main\map_menu.xml` and game archives also prove the
native Radio presentation contract. `MM_WaveformRect` is a 350x350 region with
`tickmark_horiz.dds` and `tickmark_vert.dds`; its inactive state shows only a
two-pixel center line. The active `MM_Waveform` is a 350x400 NIF positioned at
Y=-40. `meshes\interface\radio wave\radiowave.nif` drives a texture-translation
controller over `textures\interface\radio wave\wave.dds`. The lower Radio page
therefore uses those installed assets and advances the wrapping U translation
only when the selected station carries the live tuned/enabled state. Merely
selecting an inactive signal never starts a panel animation timer.

Reference-only extra-data information: radio sources can carry `ExtraRadioData` (old type `0x68`) containing radius/range/static percentage/position reference. The current type and layout need a startup/runtime canary before use.

## RTTI and vanilla menu oracles

The following current MSVC RTTI chains were resolved from type descriptors, complete-object locators, and vtables:

| Type | TypeDescriptor | COL | primary vtable | first virtual |
|---|---:|---:|---:|---:|
| `InventoryMenu` | `0x01211714` | `0x01062514` | `0x00F9A838` | `0x00939480` |
| `StatsMenu` | `0x01204E40` | `0x01061990` | `0x00F992A8` | `0x009103A0` |
| `MapMenu` | `0x0121187C` | `0x010627C0` | `0x00F9B1DC` | `0x00945100` |
| `Menu` | `0x01204E10` | `0x01074048` | `0x0100675C` | `0x00B50620` |
| `FOPipboyManager` | `0x01210F8C` | `0x0106163C` | `0x00F97530` | `0x008EF5C0` |
| `PlayerCharacter` | `0x01204AEC` | multiple | `0x00F93958` primary | `0x0057AFD0` |
| `TESQuest` | `0x012039E8` | multiple | `0x00F77C80` primary | varies |
| `BGSQuestObjective` | `0x012060A4` | current COL | `0x00F44638` | current destructor |

### Implemented live-data pass (2026-08-30)

The native bridge now validates and consumes `DataHandler**` at RVA
`0x00E249E4`, enumerates active `TESQuest` records and displayed objectives,
resolves the tracked quest from `PlayerCharacter+0x614`, and enumerates owned
`BGSPerk` records through the exact `HasPerk` evaluator. Quest tracking writes
only the two vanilla player selection fields and only in response to an
explicit lower-screen action.

Player vitals are independently validated per actor value. A failed optional
field no longer suppresses HP, AP, weight/capacity, DR, radiation, SPECIAL, or
skills as a group. Inventory, DATA, map-position, and vitals each carry their
own revision, so movement cannot invalidate or recompose the ITEMS page.

The lower-panel input protocol supports isolated subtab/list/action navigation
(`nav left/right/up/down/activate`) in addition to the existing three-screen
page transition. WEMU and Dualscreen Companion send those commands only while
Select is held, preserving ordinary controller input for gameplay.

`PlayerCharacter` has multiple secondary vtables; do not accept a pointer merely because it equals one of them. Verify the primary vtable/type, reference FormID/type, and gameplay readiness.

Current menu virtual landmarks:

```text
InventoryMenu: click 0x939750, update 0x93A3E0, controller 0x93A350
StatsMenu:     click 0x973720, update 0x9743F0, controller 0x974380
MapMenu:       click 0x947C10, wheel 0x949B90, update 0x949670
```

These are behavior oracles and possible low-volume dirty-event probes only. The custom second screen must not depend on a vanilla menu being opened or on `FOPipboyManager` having completed menu-specific initialization.

## Input and render lifecycle

Static imports establish DirectInput, XInput, USER32, and D3D9/D3DX use, but they do not reveal the live DXVK device/swap-chain addresses. Locate `IDirect3DDevice9::Present`/`Reset` from the live device vtable after the renderer exists; do not patch an assumed import.

Recommended lifecycle:

```text
proxy load (minimal, no loader-lock work)
  -> worker/bootstrap thread
  -> wait for supported module/hash and game window
  -> discover validated globals/RTTI/command anchors
  -> wait for non-null validated player and cell
  -> install small game-thread dirty/action hook
  -> create independent lower-display UI/render endpoint
  -> publish immutable state snapshots
```

Reset/lost-device handling must release only mod-owned default-pool resources, recreate them after reset, and never release or retain game-owned COM objects without balanced ownership. The first implementation should render the lower UI through its own WEMU/native lower-display surface where possible. If a separate D3D9 device/window is used, it must not change the game's device render state, viewport, camera, or framebuffer.

Touch input received by the lower window must be consumed there. It must not be translated into top-window mouse movement/clicks. Controller focus should be arbitrated at the WEMU/lower-UI layer; while lower UI has focus, consume only the intended navigation buttons and do not synthesize vanilla Pip-Boy input.

## Phase 2 minimum anchors: signatures, masks, and gates

Local file used for this analysis:

`C:\Users\Shadow\AppData\Local\Temp\wemu-fallout3-recon\Fallout3-current.exe`

The following signatures are for the exact 1.7.0.4 hash above. In the mask notation, `x` means the byte must match and `?` means decode/validate the operand. Scanning is a secondary defense; exact-hash RVAs plus structural validation remain the primary route.

### Player singleton

Use the distinctive native-equip call site inside the command handler:

```text
pattern:
6A 01 FF 75 E4 8B CB 6A 01 6A 00 6A 01 FF 75 EC E8 00 00 00 00 3B 1D 00 00 00 00
mask:
xxxxxxxxxxxxxxxxx????xx????
```

Decode the `E8 rel32` as the current equip native and the absolute operand after `3B 1D` as `PlayerCharacter**`. Expected exact-build results are equip VA `0x007F0A30` and singleton VA `0x0123C674`.

Validation predicates:

- Match lies in `.text` and belongs to the `EquipItem` execute pointer obtained from a structurally valid command descriptor.
- Decoded native target lies in `.text` and matches the equip prologue/call behavior documented above.
- Singleton storage lies in writable `.data`.
- Before a world is ready, null is valid. After readiness, player points to committed readable memory.
- Primary vtable points into `.rdata` and resolves through the current `PlayerCharacter` RTTI chain.
- `player+0x0C` is stable across two consecutive game-thread samples and is the player reference FormID (`0x00000014`) for the tested vanilla runtime.
- `player+0x3C` is either null during transition or a readable current cell; never dereference beyond this until readiness stabilizes.

**Gate:** strong enough for first read-only deployment. Any failed predicate disables all game-state reads. The decoded equip target itself remains action-gated.

### Main-thread update boundary

Static analysis identified the current WinMain/game-loop function at VA/RVA `0x009B31A0` / `0x005B31A0`. It registers WndProc `0x009B2E00`, creates the game window, drains `PeekMessageA`/`TranslateMessage`/`DispatchMessageA`, and has a backward frame-loop edge from `0x009B3FE4` to `0x009B3CC0`.

The first active-update block immediately after message dispatch begins at VA/RVA `0x009B3D95` / `0x005B2D95` and has this relocatable signature:

```text
pattern:
8B 0D 00 00 00 00 E8 00 00 00 00 E8 00 00 00 00 6A 00 E8 00 00 00 00 8B 0D 00 00 00 00 83 C4 04 6A 01 E8 00 00 00 00
mask:
xx????x????x????xxx????xx????xxxxxx????
```

Expected call sequence is a per-frame manager update, `0x008E2CB0`, radio-state refresh `0x008E6750(false)`, then another manager update. The labels of the two manager calls are intentionally not guessed.

Structural validation predicates:

- The signature is unique in `.text`.
- It is contained in the `0x009B31A0` function described above.
- The enclosing function references the USER32 IAT entries for `PeekMessageA`, `TranslateMessage`, and `DispatchMessageA` and has the proven backward loop edge.
- At runtime the executing thread ID equals `GetWindowThreadProcessId(gameWindow)`.
- A diagnostic counter advances during normal gameplay and stops while the owning window thread is blocked; no heavy work runs inline.

For the **first read-only deployment**, do not detour this large loop. Use a window-bound `WM_TIMER` callback and verify the callback thread ID equals the game-window owner. This provides a game-thread execution boundary without patching `.text`. The static loop anchor is retained as the later low-jitter/event-pump option. A binary detour is action-gated until trampoline length, exception unwinding, resume address, save/load transitions, and Wine/DXVK behavior are proven.

### ActorValueOwner calls

The current `GetActorValue` eval path contains:

```text
pattern:
8B 46 08 57 8B 7D 0C C1 E8 0B 57 A8 01 75 00 8D 8E 9C 00 00 00 EB 00 8B 4E 1C 81 C1 00 01 00 00 8B 01 8B 40 0C FF D0
mask:
xxxxxxxxxxxxxx?xxxxxxx?xxxxxxxxxxxxxxxx
```

This decodes the actor owner displacement `+0x9C` and the current-float virtual slot `+0x0C`. The alternate non-actor branch obtains an owner at `baseForm+0x100`; the player reader must take the actor branch only.

Validation predicates:

- Match belongs to the `eval` pointer of the validated `GetActorValue` command descriptor (opcode `0x100E`).
- Player has already passed singleton/RTTI/readiness validation.
- `player+0x9C` is readable, and its vtable and called slot point into the game module's `.rdata`/`.text` respectively.
- Query only metadata-validated IDs `0..0x48`.
- Results must be finite. Canary checks should include plausible SPECIAL `5..11`, non-negative maximum/current HP, and bounded radiation/limb values. A canary failure disables actor values without disabling unrelated UI.
- Call only on the validated game-window thread.

**Gate:** strong enough for first read-only deployment. Actor-value mutation virtuals and the `SetActorValue` command path are not authorized and remain disabled.

### Inventory root and traversal

The pure current `ExtraContainerChanges` payload getter has this high-specificity signature at VA/RVA `0x005D2660` / `0x001D2660`:

```text
pattern:
6A 15 E8 00 00 00 00 85 C0 74 04 8B 40 0C C3 33 C0 C3
mask:
xxx????xxxxxxxxxxx
```

It tail-calls/uses the generic extra-data list lookup with type `0x15` and returns `[extra+0x0C]` or null. Invoke it read-only with `ECX=player+0x40` on the validated game thread. Do not use the get-or-create helper at `0x00622290`.

Traversal predicates:

- Returned null is a valid empty/not-ready state.
- Returned data is committed/readable and `data+0x04` owner is null or the validated player/current container owner expected for this snapshot.
- `[data+0x00]` is the pointer to the entry-list head. Each node is readable and shaped as `{EntryData* item, Node* next}`.
- Each `EntryData` has `extendData+0x00`, signed `countDelta+0x04`, and `TESForm* type+0x08`, as independently exercised by current `0x00623FC0`.
- Each form pointer is readable, its vtable points into `.rdata`, `form+0x04` is a known current form type, and `form+0x0C` is a plausible FormID.
- Bound traversal (recommended hard ceiling 8,192 nodes/stacks), use a visited set or tortoise/hare cycle guard, and abort the snapshot on corruption rather than following arbitrary memory.
- Stack `extendData` lists receive the same pointer/cycle bounds before checking worn/count/health extras.
- Perform no allocation through engine helpers and copy only primitive fields/strings into the immutable snapshot.

**Gate:** the root and entry traversal are strong enough for first read-only deployment. Stack-specific metadata remains field-gated until worn/count/health extra type/layout canaries pass. All inventory mutations remain action-gated.

### Deployment strength summary

| Anchor / subsystem | First read-only deployment | Action enabled |
|---|---|---|
| exact hash/PE/sections | Yes; mandatory global gate | n/a |
| player singleton | Yes, after all pointer/RTTI/readiness predicates | No write through player fields |
| window-owned main-thread timer | Yes, after thread-ID proof | May drain bounded queue later |
| static main-loop detour | Not needed; diagnostic discovery only | No, until trampoline/runtime proof |
| ActorValueOwner current/base reads | Yes, finite/range-canary gated | No AV writes |
| inventory root and bounded entries | Yes | No |
| worn/count/health stack extras | Only after per-type runtime canaries | No |
| quest/DataHandler reads | Yes after list/string bounds and current flag validation | Active-quest setter remains gated |
| marker-extra reads | Yes after marker type/flag correlation | No marker writes |
| direct equip/unequip | No | Gated: exact stack + six-argument semantics + one-action matrix required |
| radio tune/off/refresh | Read current state only | Gated: availability/lifecycle matrix required |
| command `execute` entrypoints | No | Never call with fabricated VM context |

## Exact early-startup, DirectInput proxy, and renderer gates

This section is specific to the installed `1.7.0.4` executable identified at
the top of this document. It records two independent early-exit mechanisms
which must not be conflated.

### DirectInput is constructed before the D3D renderer

The executable imports only `DINPUT8.dll!DirectInput8Create` at IAT VA
`0x00F2402C`; the import thunk is VA/RVA `0x00DA52A1` / `0x009A52A1`.
There is one direct call, at VA/RVA `0x009DCF09` / `0x005DCF09`, in the input
constructor beginning at `0x009DCEB0`.

The arguments at that call are:

- the supplied application `HINSTANCE`;
- DirectInput version `0x0800`;
- IID bytes at `0x01046BF4`;
- output pointer `this+0x08`;
- null aggregation outer.

Fallout does **not** test the returned HRESULT. At `0x009DCF40` it loads
`[this+0x08]`, then at `0x009DCF57` calls through that interface vtable to
create the first device. Therefore a proxy which successfully loads but cannot
resolve the real export and returns `DIERR_NOTFOUND` with a null output causes
an immediate null-interface fault. It is not a recoverable no-controller path.

The ordering is also statically proved. Main startup calls the containing input
initializer (`0x009A8AE0`) at `0x009B3844`. It does not call the renderer
factory (`0x006B9FC0`) until `0x009B38C5`. A broken `DirectInput8Create`
forward can consequently kill the process before DXVK creates a D3D9 device.

The packaged proxy is PE32/i386 and exports `DirectInput8Create` ordinal 1 plus
the four expected COM exports. Its imports are ordinary Wine/Windows system
DLLs. Its lazy initializer currently starts the mod worker, then calls
`LoadLibraryW(<system-directory>\\dinput8.dll)`. A load returning the proxy's
own handle, a failed load, or a missing `DirectInput8Create` export all lead to
`DIERR_NOTFOUND`, which is unsafe for this exact caller. The worker also begins
before Fallout has a valid input manager or renderer; runtime hooks must remain
fully readiness-gated at this point.

Safest proxy A/B proof, with no executable or prefix rebuild:

1. Confirm Fallout/Wine are inactive; hash the current proxy and preserve it by
   an atomic same-directory rename.
2. Keep the exact executable, prefix, `native,builtin` AppDefaults value,
   command line, working directory, preferences, and container environment
   unchanged. This leaves Wine's 32-bit system `dinput8.dll` as the fallback.
3. Launch once and capture process lifetime, Wine `+loaddll,+module` output,
   any unhandled fault, foreground-window adoption, and DXVK log creation.
4. Restore the byte-identical proxy by atomic rename and repeat once under the
   same conditions.
5. Interpret only the differential: system `dinput8` reaching the renderer
   while the proxy run dies before renderer/DXVK makes the proxy path causal;
   both reaching DXVK and exiting later means the renderer/config gate below is
   independent. Absence of `Fallout3SecondScreen.log` alone is not proof that
   the DLL did not map, because log creation can fail separately.

The observed device was already in this clean B state during the latest
read-only check: the proxy hash
`2f35154fae6cee1a2ddb69697e73389568aabc1b1b0e025fcbf798ce1394df09`
was parked as `.dinput8.dll.fo3ds-disabled-ab`, the per-executable override
remained `native,builtin`, and the prefix's 32-bit `syswow64/dinput8.dll` was
present.

### Preferences, launcher registry, and fullscreen resolution

Startup constructs the user configuration root with `SHGetFolderPathA`. With
`bUseMyGamesDirectory` enabled it uses CSIDL `5`, appends `\\My Games\\` and
`Fallout3\\`, and resolves the active `FALLOUT.INI` and `FalloutPrefs.ini`.
The exact executable can copy `Fallout_default.ini` when the selected INI is
absent. Thus a missing preferences file is not a loader failure by itself, but
its default/empty display settings can activate later gates.

At `0x009B3502`, Fallout can take a renderer-device mismatch path. It opens
`HKLM\\Software\\Bethesda Softworks\\Fallout3`, queries `Installed Path`,
appends the literal `FalloutLauncher.exe`, and calls `ShellExecuteA`. The
current install has a valid Wow6432Node `Installed Path` value but contains
`FalloutLauncherSteam.exe`, not the literal `FalloutLauncher.exe`. This path
can therefore return from game startup without ever making a gameplay window.
The current `sD3DDevice` value must match the adapter description seen by this
Wine/DXVK run; do not blindly preserve a desktop-PC adapter string.

The live configuration was mismatched at audit time: xuser-2's container set
DXVK `gpuName=NVIDIA GeForce GTX 480`, while its active `FalloutPrefs.ini` set
`sD3DDevice="AMD Radeon RX 6700 XT"`. For that exact container, the safe
preference value is `sD3DDevice="NVIDIA GeForce GTX 480"` unless a captured
D3D9 adapter description proves otherwise. Changing the container spoof instead
is valid only if the preference is changed to the same resulting description.

The renderer factory copies `iSize W:Display` and `iSize H:Display` from values
at `0x011047F0` and `0x011047FC`, then calls the display-mode validator at
`0x006BB280`. In fullscreen (`bFull Screen` value at `0x01104814`), that
validator:

- obtains primary width/height via `GetSystemMetrics(0/1)`;
- enumerates the selected D3D adapter's display modes;
- requires a compatible format and at least 32 bits per pixel;
- accepts mode aspect only within `0.01` of the primary aspect, apart from a
  narrow legacy `1.25`/4:3 special case;
- requires candidate width equal to configured width and candidate height at
  least the configured height, preferring exact/closest height.

Consequently `960x600` fullscreen (16:10) is rejected when the primary is
`1280x720` (16:9), even if a nominal 960x600 mode exists. No candidate leaves
the message `Your display doesn't support the selected resolution...` and
returns false. `bIgnoreResolutionCheck:Display` has no direct `.text`
reference in this exact executable and is not read by `0x006BB280`; setting it
is not a proved bypass. The exact non-binary correction is:

```ini
iSize W=1280
iSize H=720
bFull Screen=1
```

Windowed mode bypasses this fullscreen mode selection and is useful only as a
diagnostic. It is not the intended final dual-screen configuration.

After mode validation, `0x006B9FC0` calls the NiDX9 renderer creator
`0x00D45B40` at `0x006BA213`. A null result flows through `0x006BA34A` to the
main failure branch at `0x009B38CD`, which displays `Failed to initialize
renderer.` and calls `ExitProcess(0)` at `0x009B3903`. This is a graceful
post-DXVK exit with no crash dump and can look exactly like the launcher
returning to its game page. Existing proxy-absent evidence reached DXVK and
then exited, so that specific baseline observation cannot be assigned to the
mod proxy.

`0x00D45B40` dynamically loads `D3D9.DLL`, resolves
`Direct3DCreate9`, and calls it with SDK version `0x20`. It returns null if the
DLL/export/instance is unavailable, if its `0xB80`-byte renderer allocation
fails, or if renderer initialization at `0x00D4AF40` returns false. The exact
binary's diagnostic strings identify the latter failure families: invalid
device ID/type or adapter, unavailable requested/reference/hardware device,
framebuffer or depth/stencil format mismatch, fullscreen-mode mismatch,
device-cap query failure, and missing single-pass multi-texture support. A
clean run should capture Gamebryo `OutputDebugString`/Wine relay output around
these messages; seeing only the final user-facing message does not identify
which internal predicate failed.

### Exact executable/package dependency check

There are no delay imports. In addition to Windows/Wine system APIs, the exact
executable imports:

- game-local `binkw32.dll` (nine Bink exports);
- game-local `libvorbisfile.dll` (six Vorbisfile exports);
- `d3dx9_42.dll` (25 D3DX shader/texture/matrix exports);
- XInput 1.3 ordinals 2 and 3;
- DirectInput 8, DSound ordinal 11, D3D9, WinMM, and OLE/USER32 support.

The live read-only package check found both game-local middleware DLLs. The
active 32-bit prefix contains `syswow64/d3dx9_42.dll`, `xinput1_3.dll`,
`dinput8.dll`, `dsound.dll`, and `d3d9.dll`. `Fallout3.exe`,
`Fallout_default.ini`, `Data/Fallout3.esm`, and the base Misc/Sound BSAs are
also present. This rules out those specific loader dependencies as the current
proxy-absent failure cause; it does not prove every later asset/archive read.

## Event-driven synchronization plan

The engine pointers are single-threaded state. Detours should do no allocation, serialization, file I/O, UI calls, or full scans. They should set atomic dirty bits and return.

Suggested sources:

| Domain | Source | Snapshot policy |
|---|---|---|
| HP/AP/radiation/current position | bounded game-thread sampler using actor-value owner | 5-10 Hz, publish only changed primitives; position may be lower rate unless map visible |
| inventory | container-change/equip/use dirty hooks | rebuild only after dirty; defensive generation check every 1-2 s |
| equipment | native equip/unequip completion plus worn-stack comparison | rebuild equipment subset only |
| quests | `SetStage`, objective mutator, active-quest setter | rebuild changed quest/objective subset |
| map markers | marker state mutation/cell change; low-rate fallback | rebuild marker delta; pan/zoom is UI-only |
| radio | `0x8E6590`, `0x8E52B0`, `0x8E6750`, cell change | update station/current-radio subset |
| UI | immutable snapshot generation | diff on UI thread; never write into game during refresh |

Every load/new-game/death-reload transition must increment a generation token and invalidate all queued pointer-bearing actions. The UI retains IDs and copied strings, not engine pointers.

## Concrete implementation sequence

1. Add an exact-build detector for SHA-256 `c3f97c...fbd2f3e`; log PE machine, image base, relocation delta, and which structural anchors pass.
2. Build read-only `SafeRead`/page validation and typed canaries. Fail closed per subsystem.
3. Resolve player and DataHandler globals by validated handler patterns; prove player FormID/type/vtable and DataHandler quest-list invariants.
4. Implement a game-thread immutable snapshot for actor values and one inventory traversal. No actions yet.
5. Validate worn/right/left stack flags using an in-game equipment change matrix.
6. Add one-shot equip/unequip commands through the direct native calls and prove exactly one action per press.
7. Add quests through current layouts and hook-only dirty bits; add active quest through `0x8C4380`.
8. Add radio list snapshot/tune/off through the current radio path.
9. Trace `MapMenu::SetTile` data producers to finish map coordinate/bounds/local-raster extraction without opening the menu.
10. Add render/reset hooks only after the data path is stable, then profile hook cost and snapshot duration.

## Runtime proof matrix still required

Before calling a subsystem complete, capture logs for:

- New game and two different saves: player generation, valid primary vtable, cell transitions.
- Actor values: SPECIAL, HP damage/heal, AP spend/recover, radiation change, XP gain, level-up, limb damage.
- Inventory: pickup, drop, stack split, duplicate damaged weapons, ammo change, Aid consumption, buy/sell, save reload.
- Equipment: right weapon, left weapon, apparel, unequip, switch, broken item, duplicate stacks. Confirm one press produces one native action.
- Quests: start, stage advance, objective display, objective complete, active quest switch, completed quest filtering, localized strings.
- Map: exterior/interior, player movement, visible-undiscovered marker, discovered marker, fast travel, local-map generation, save switch.
- Radio: no stations, one/multiple stations, tune, off, leave range, interior/exterior, scripted station state change.
- Renderer: device reset, resolution change, alt-tab/focus, loading screen, death/reload, long session, lower-display disconnect/reconnect.

Instrumentation should record count, total time, max time, cache hit/miss, and snapshot generation. Aggregate/rate-limit logs rather than printing every frame.

## Known unknowns / do not guess

- Exact current parameter meanings for all three booleans in the six-argument equip/unequip natives.
- Current exact type IDs/header offsets for every `BSExtraData` subclass, although the enclosing reference/container paths are strongly supported.
- Current field offsets/getters for every weapon, apparel, Aid, ammo, note, perk, and effect attribute.
- Current active-effect and perk list ownership/lifetime.
- Exact objective/quest flag names beyond the statically proved displayed/completed objective bits.
- Marker bit 1's precise name (discovered vs fast-travel eligible) and marker `+0x0E` enum semantics.
- Local-map producer object, ownership, texture format, and safe generation trigger when the vanilla menu has never opened.
- Data structure for notes/audio transcripts and station display names.
- Exact live D3D9/DXVK device/swap-chain and WEMU lower-surface handoff.
- A safe current-build lookup-by-FormID helper. Until found, resolve selected objects from the current container/quest/radio snapshot on the game thread.

An unknown should disable only that feature and produce one actionable diagnostic. It must not cause a fallback to fake data, a hidden vanilla menu, a screenshot, or a guessed write.

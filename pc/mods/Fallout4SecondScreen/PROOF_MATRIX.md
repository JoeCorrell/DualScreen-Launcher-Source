# Fallout 4 second-screen proof matrix

Nothing becomes `Complete` until it is reproduced in a running loaded save.

| Area | Required runtime proof | Status |
|---|---|---|
| Runtime gate | Detect version/hash/module set and fail closed on mismatch | Complete: device trace identified 1.11.240.0 and exact executable SHA while Fallout4.exe remained running |
| Native state bridge | Read all 11 populated `PipboyDataGroup` trees with the Pip-Boy closed; event coalescing and manager-generation recovery | Exact 1.11.240 layouts, bounded parser tests, and compile checks pass; loaded-save game proof pending |
| Lifecycle | Main menu, new game, load save, reload, death, fast travel, cell change | F4SE host simulation passes; game pending |
| STAT | HP/AP/radiation/level/XP/caps/weight/SPECIAL/effects change live | Pending |
| STAT artwork | SPECIAL 0..6, BodyFlags 0..16, HeadFlags 0..31, and live perk clipName/SWFFile select authentic user-owned Fallout 4 art | Archive paths/schema confirmed; 159-asset cache generated and loader tests pass; device visual/state proof pending |
| Inventory sync | Pickup, drop, equip weapon, apparel on/off, Aid use, buy/sell, container transfer, different save | Pending |
| Inventory actions | Each UI action changes authoritative game state and survives next update | Pending |
| DATA | Start/update/complete/track/untrack quests and load different saves | Pending |
| World map | Walk, rotate, discover, quest/custom markers, zoom/pan, fast travel | Pending |
| Local map | Interior/exterior transitions, extents, player arrow, doors | Pending |
| RADIO | Discover/range, tune, switch, turn off | Pending |
| Workshops | Ownership and resource ratings update after settlement changes | Pending |
| Lower renderer | Independent surface through load/dialogue/combat/save/death; no flicker/focus/camera change | Startup proven on device at 0,720 1240x1080 with isolated touch socket; loaded-save lifecycle matrix pending |
| Touch isolation | All gestures operate only the lower UI; no attack/look/move/menu leak | Strict parser, loopback rejection, reserved console strip, terminal swipe/scroll direction lock, and malformed-stream tests pass; device pending |
| Controller routing | Predictable navigation with explicit capture and no unintended gameplay suppression | Pending |
| Performance | CPU/GPU/allocation/network/frame-time/memory profile including long session | Exact 1 MiB stack probe/build frame guard pass; three-surface transition cost pending device profile |
| Failure handling | Service/F4SE/renderer/input failure degrades to diagnostic UI without crashing Fallout | Bootstrap stack-overflow fixed and exact-stack/device launch regression passed; later lifecycle cases pending |

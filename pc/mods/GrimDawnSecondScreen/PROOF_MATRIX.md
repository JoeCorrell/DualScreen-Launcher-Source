# Proof matrix

`static` means supported by the exact installed binaries or device files.
`host-tested` means deterministic local tests passed. `device-live` is required
before a capability can be treated as complete.

| Capability | Evidence | Status |
|---|---|---|
| Exact launch target | Captured live command `F:\Games\Grim Dawn\Grim Dawn.exe`; PE I386 | static |
| Exact version | Grim Dawn log and crash metadata report 1.3.0.7 | static |
| Base + GDX1/2/3 content | Live process held all corresponding ARC/ARZ archives | static |
| Unknown-build fail-closed | SHA-256/size/timestamp/machine gate; unrelated test host rejected | host-tested |
| DXGI transparency | PE32 proxy loads system PE32 DXGI and forwards a real factory request; unrelated host creates no hook/panel | host-tested |
| Game-thread seam | Main EXE imports exported `GameEngine::Update`; exact IAT lookup and compare-exchange | static + host-tested IAT parser/store; device-live pending |
| HP / Energy | Exported Character getters and limit getters | static; device-live pending |
| Level / XP / name | Exported Character/Player getters | static; device-live pending |
| Attributes / OA / DA | Exported `Character::GetTotalCharAttribute`; enum 1/2/3 and 21/23 recovered from named attribute constructors | static high-confidence; device-live pending |
| Iron Bits | Exported `Character::GetCurrentMoney` | static; device-live pending |
| Inventory count | Exported real `mem::vector<uint32_t>` with bounds/readability checks | static; device-live pending |
| World position | Exported `Character::GetPathPosition`; I386 `WorldVec3` is Region pointer + XYZ, 16-byte hidden-sret return | static; device-live pending |
| Area | Exported `Engine::GetAreaNameTag`, exact VS2015 x86 string decoding | static; device-live pending |
| Independent panel | Separate no-activate GDI/DIB HWND on non-primary monitor | host-built pending; device-live pending |
| WEMU lower ownership | Exact class/title early claim in ASurface and compositor | WEMU unit-tested; device-live pending |
| Touch isolation | Strict loopback parser; WEMU legacy X injection blocked for Grim | host-tested + WEMU unit-tested; device-live pending |
| Controller isolation | Select-modified routing; unused Select replay; bare controls preserved | WEMU unit-tested; device-live pending |
| Equipment and item definitions | Entity-to-record resolution incomplete | unavailable |
| Skill/Devotion primitives | Exported point getters plus bounded Skill-pointer vector and Devotion totals | static; device-live pending |
| Full skills / Devotion graph | Resource-name digests, ranks/cooldowns and ARZ correlation not yet implemented | unavailable |
| Quests / Factions / Crafting | Manager exports identified; live structure validation incomplete | unavailable |
| Known Riftgate count | Per-difficulty `Player::GetTeleportUIDs`, 16-byte `UniqueId` vector | static; device-live pending |
| Map / Riftgate records / exploration | Static archives and runtime APIs identified; independent reconstruction incomplete | unavailable |
| Mutating actions | Command execution intentionally not compiled into diagnostic milestone | unavailable |
| Long-session stability | Requires device deployment and gameplay matrix | pending |

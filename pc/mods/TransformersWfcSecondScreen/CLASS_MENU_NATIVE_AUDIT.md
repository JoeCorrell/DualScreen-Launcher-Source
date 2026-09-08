# Campaign class-menu native dispatch audit

Supported executable: retained `TWFC.exe`, SHA-256
`1db04b09b36094571fab861cd01ca175d44d610b5417f950f37523cd97eeb016`.
Addresses below are preferred-image VAs (image base `10900000`), not device
runtime pointers. Sources are the retained executable, cooked `TransGame.xxx`,
and extracted CharacterSelect/CustomTransformers ActionScript. No gameplay
verification or deployment was performed for this audit.

## Entry and actual blocker

`TnGameLobbyGame.StartLevel` (cooked20991, native10A9B300) is already intercepted
by NativeCampaignTravel. CampaignClassSystem still admits its custom flow only
after all required menu/data/spawn adapters install. Several required methods
are compiled with stripped reflection. The missing menu is therefore not
evidence that `HmUtility.OpenCharacterSelect` itself is an absent game feature.
Removing the admission gate would expose a stock editor with unadapted write,
spawn, and resolve operations and would not implement a custom campaign squad.

`HmUtility.OpenCharacterSelect` is an actual ActionScript function, in both
extracted movies' `scripts/__Packages/HmUtility.as`:

- Reads `<CurrentGame:_bIconicMode>`.
- True: creates `_root.charSelect_mc`, asynchronously loads
  `../CharacterSelect/CharacterSelect.swf`.
- False: creates `_root.customCharMenu_mc`, asynchronously loads
  `../Customize/CustomTransformers.swf`.
- `selectParamArray` is copied on `MovieClipLoader.onLoadComplete`, not before
  the call returns. Do not assume the child controls exist synchronously.

The current custom flow intentionally keeps authored mission-shell identities
and overrides editor/spawn data. The stock movies do not implement selection
of three campaign squad members; that sequencing is mod-owned.

## Implemented native FString-return wrapper family

`NativeCampaignMenu.cpp` replaces three stripped reflection dependencies:

| Binding method | Registered wrapper | Actual body | Registration FName |
| --- | --- | --- | --- |
| GetCurrentCharacter | 10BEBD00 | 10BE8C30 | 12B30A50 |
| GetCustomCharacters | 10BEBBE0 | 10BE8690 | 12B30A58 |
| GetPlayableCharacters | 10BEBC30 | 10BE87C0 | 12B30AB4 |

All three wrappers are exactly78 bytes with the same instruction/lifetime
shape. They differ in three relative CALL operands; two absolute allocator
references need image-base relocation. The implementation validates the entire
body before installing, including all CALL targets and RET8.

ABI: ECX is the compiled binding base; stack arguments are FFrame pointer then
initialized FString result pointer. Wrappers ignore ECX. Each obtains a
temporary FString through a hidden output pointer, assigns it into the result
using10901150, then releases the temporary through the game allocator.

The wrapper is deliberately intercepted rather than10BE8C30: the latter's
hidden result storage has not yet been constructed. Calling the mod's FString
assignment helper on that storage would be an ownership bug.

GetCurrentCharacter/GetCustomCharacters use the currently edited slot only
while the complete campaign flow admits the loadout stage; otherwise stock
behavior runs unchanged. GetPlayableCharacters always runs stock first, then
passes its initialized output to the existing roster filter. Compiled binding
is converted to raw UObject by subtracting10 only for the post-filter callback.
Null sentinel, wrong-thread, nested callbacks, and stopping hooks pass through.
Stopping pins the trampoline; it never frees code under an active native stack.

Focused verification: all234 retained wrapper bytes, single-byte corruption
rejection, relocation checking, real32-bit detour/trampolines,2,000 iterations
through each of three wrappers, nested callbacks, wrong-thread stock forwarding,
and shutdown. CampaignClassSystem passes strict MinGW syntax checking. Output
storage in ABI fixtures is an integer: this is not engine FString allocation,
Scaleform, chapter loading, or gameplay testing.

## Adjacent dispatch evidence, not yet migrated

Static registration blocks10A49xxx..10A4Axxx associate class/name globals and
wrapper pointers through1170A880. Class global12B33AD4 is the character binding.
This is a coherent source of entry addresses, but does not reconstruct stripped
parameter UProperties, nested return struct ownership, or custom campaign
semantics. Do not teach a generic resolver to invent those contracts.

| Method | Wrapper | Evidence / remaining contract |
| --- | --- | --- |
| SelectCharacter | 10BEBCE0 | FFrame+10 gives locals; FString at locals+0; calls10BE8B00; RET8 |
| ClearCharacter | 10BEBCF0 | Calls10BE8BB0; RET8 |
| CommitCharacter | 10BEBD50 | Ten FString inputs at locals+0,+C,+18,+24,+30,+3C,+48,+54,+60,+6C; calls10BE8CC0; RET8 |
| GetCharacterSkills | 10BEBA60 | String-input/return wrapper; requires separate full-body/lifetime verification |
| GetCharacterWeaponTypes | 10BEBAC0 | Same family candidate, not assumed interchangeable |
| GetCharacterVehicleWeapon | 10BEBB20 | Same family candidate, not assumed interchangeable |
| GetCharacterAbilities | 10BEBB80 | Same family candidate, not assumed interchangeable |
| WriteCustomizationFile | 10BEBD90 | Must remain suppressed during mod-owned campaign editing |
| ResetCharacter | 10BEBDD0 | Native multiplayer state mutation requires scoped adaptation |
| UnlockPrimeMode | 10BEBF30 | Must not unlock global/profile progression for campaign use |

SelectCharacter's actual binding body10BE8B00..10BE8BA7 takes FString pointer on
the stack and returnsRET4. It gets the native local player via the engine's
world, determines iconic mode, and calls controller SelectCharacter10B20FE0.
It then conditionally notifies lobby virtual+390. The controller call's ABI is
EAX=compiled player controller, EDX=FString pointer, one stack role argument.
Controller registration wrapper10B2B0A0 extracts role byte at locals+C and
writes a return value. This is a chapter selection operation, not possession.
It resolves selected character data and can alter PRI/checkpoint override state.
Do not use it as an unrestricted mid-mission character switch.

The custom flow's existing ten-argument CommitCharacter adaptation must stay
connected to actual movie calls. Extracted customize scripts pass ten values,
including friendly name/chassis/specialty/colors, followed by skill/weapon/
ability/vehicle selections. A five-string substitute would shift parameters.

## Chapter/checkpoint keys and current persistence gap

Verified cooked members/functions:

- `TnGameReplicationInfo.SelectedMapID`, IntProperty21168.
- `GetMapID`, function21242: returns SelectedMapID.
- `SetMapId`, function21240: assigns it, sets force net update and refreshes
  game data store. Reading the property does not require this mutation.
- `TnGameReplicationInfoGameLobby.SelectedCheckpoint`, StrProperty21010.
- `ModifyURL`, function21460: prepends GetMapFilename, appends `?MapId=` plus
  GetMapID, and nonempty `?Checkpoint=` plus SelectedCheckpoint.
- `StartLevel`, function20991: GameSettings.BuildURL then LobbyGRI.ModifyURL
  before TnGame.ServerTravelToMap.

Capture the validated lobby map ID/checkpoint before releasing travel. They
are better chapter/checkpoint keys than pawn pointers or roster-array order,
but are not sufficient to distinguish user profiles/save slots by themselves.
GetMapFilename is not declared on base TnGameReplicationInfo; do not infer a
base-class property offset from the call in the lobby override.

Current `CommitConfiguration` only writes process-local `g_configuration`:
mission_shells=first three native roster entries; selected_characters=choices;
initial_slot=chosen member. `native_initial` remains mission_shells[initial_slot].
ResolvedCharacterCache retains native return-frame buffers and nested struct
views, not a durable serialized save. `CampaignClassSelectedCharacterName`
exposes only shell-to-selected-name mapping. There is no durable chapter/save
key or persistence implementation in this code today. Checkpoint capture uses
the controller's selected chapter character, so changing it merely to display
the actively controlled squad member would change checkpoint semantics.

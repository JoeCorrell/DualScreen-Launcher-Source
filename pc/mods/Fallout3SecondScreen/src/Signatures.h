#pragma once

#include <cstdint>

namespace fo3ds::sig {

constexpr uintptr_t kScriptTableRva = 0x00D0B388;
constexpr uintptr_t kGetActorValueDescriptorRva = 0x00D0B5B8;
constexpr uintptr_t kGetStageDescriptorRva = 0x00D0BC98;
constexpr uintptr_t kEquipDescriptorRva = 0x00D0D8B8;
constexpr uintptr_t kUnequipDescriptorRva = 0x00D0D8E0;
constexpr uintptr_t kHasPerkDescriptorRva = 0x00D0F9B0;
constexpr uintptr_t kGetXpForNextLevelDescriptorRva = 0x00D106D0;
constexpr uintptr_t kGetActorValueExecuteRva = 0x00393080;
constexpr uintptr_t kGetActorValueEvalRva = 0x002EE640;
constexpr uintptr_t kGetStageExecuteRva = 0x003962F0;
constexpr uintptr_t kGetStageEvalRva = 0x002F4210;
constexpr uintptr_t kEquipExecuteRva = 0x003921C0;
constexpr uintptr_t kEquipNativeRva = 0x003F0A30;
constexpr uintptr_t kUnequipExecuteRva = 0x003AF560;
constexpr uintptr_t kUnequipNativeRva = 0x003F1180;
constexpr uintptr_t kHasPerkExecuteRva = 0x00396C90;
constexpr uintptr_t kHasPerkEvalRva = 0x002F5590;
constexpr uintptr_t kGetXpForNextLevelExecuteRva = 0x00396AF0;
constexpr uintptr_t kGetXpForNextLevelEvalRva = 0x002F5250;
constexpr uintptr_t kGetPcMiscStatDescriptorRva = 0x00D0E448;
constexpr uintptr_t kGetPcMiscStatExecuteRva = 0x00395980;
constexpr uintptr_t kGetPcMiscStatEvalRva = 0x002F3540;
constexpr uintptr_t kPipboyRadioDescriptorRva = 0x00D0F5A0;
constexpr uintptr_t kPipboyRadioExecuteRva = 0x0039DC60;
constexpr uintptr_t kPipboyRadioOffDescriptorRva = 0x00D10928;
constexpr uintptr_t kPipboyRadioOffExecuteRva = 0x0039FFB0;
constexpr uintptr_t kRadioEnableRva = 0x004E52B0;
constexpr uintptr_t kRadioTuneRva = 0x004E6590;
constexpr uintptr_t kRadioCandidateBuilderRva = 0x001B2990;
constexpr uintptr_t kListNodeDestructorRva = 0x0017F6D0;
constexpr uintptr_t kHeapFreeRva = 0x005D9F40;
constexpr uintptr_t kHeapManagerRva = 0x00D165DC;
constexpr uintptr_t kContainerChangesGetterRva = 0x001D2660;
constexpr uintptr_t kInventoryEntryNameRva = 0x00221EF0;
constexpr uintptr_t kTesFullNameTypeDescriptorRva = 0x00E03468;
constexpr uintptr_t kTesIconTypeDescriptorRva = 0x00E05428;
constexpr uintptr_t kTesBoundObjectTypeDescriptorRva = 0x00E0569C;
constexpr uintptr_t kTesFormTypeDescriptorRva = 0x00E03400;
constexpr uintptr_t kTesWeightFormTypeDescriptorRva = 0x00E05A54;
constexpr uintptr_t kTesValueFormTypeDescriptorRva = 0x00E05A38;
constexpr uintptr_t kTesHealthFormTypeDescriptorRva = 0x00E04530;
constexpr uintptr_t kTesAttackDamageFormTypeDescriptorRva = 0x00E044B4;
constexpr uintptr_t kTesObjectArmoTypeDescriptorRva = 0x00E04674;
constexpr uintptr_t kTesQuestTypeDescriptorRva = 0x00E039E8;
constexpr uintptr_t kBgsNoteTypeDescriptorRva = 0x00E03A34;
constexpr uintptr_t kBgsPerkTypeDescriptorRva = 0x00E03B08;
constexpr uintptr_t kTesWorldSpaceTypeDescriptorRva = 0x00E03E2C;
constexpr uintptr_t kTesObjectCellTypeDescriptorRva = 0x00E03E48;
constexpr uintptr_t kBgsQuestObjectiveTypeDescriptorRva = 0x00E060A4;
constexpr uintptr_t kActiveQuestSetterRva = 0x004C4380;
constexpr uintptr_t kQuestLogTextResolverRva = 0x00375BC0;
constexpr uintptr_t kShowQuestLogTextCallRva = 0x003A8F59;
constexpr uintptr_t kDynamicCastRva = 0x0097B79B;
constexpr uintptr_t kPlayerGlobalRva = 0x00E3C674;
constexpr uintptr_t kDataHandlerGlobalRva = 0x00E249E4;
constexpr uintptr_t kSelectedRadioGlobalRva = 0x00E393D8;
constexpr uintptr_t kRadioEnabledGlobalRva = 0x00E393F0;
constexpr uintptr_t kRadioTransitionGlobalRva = 0x00E393F2;
constexpr uintptr_t kPlayerPrimaryVtableRva = 0x00B93958;
// PlayerCharacter's MagicTarget secondary vtable and its exact virtual
// GetActiveEffectList implementation for the researched 1.7.0.4 image.
constexpr uintptr_t kPlayerMagicTargetVtableRva = 0x00B93E94;
constexpr uintptr_t kGetActiveEffectListRva = 0x002FAD30;
constexpr uintptr_t kWorldMapMarkerListBuilderRva = 0x003C8EF0;
constexpr uintptr_t kMapMarkerDataGetterRva = 0x0035D3C0;
constexpr uintptr_t kSimpleListDestructorRva = 0x0017BB00;
constexpr uintptr_t kWorldMapDataResolverRva = 0x003CB920;
constexpr uintptr_t kWorldMapTextureResolverRva = 0x003CB950;
// BGSNote variant accessors used by the vanilla Pip-Boy Notes page. The note
// payload kind lives at +0x7C and acquired PlayerCharacter notes are held by
// the embedded BSSimpleList at +0x570.
constexpr uintptr_t kNoteImagePayloadGetterRva = 0x001A58E0;
constexpr uintptr_t kNoteTextPayloadGetterRva = 0x001A5910;
// Isolated native Local Map cell-render path. These helpers render a single
// cell into their own NiRenderedTexture and do not require the MapMenu
// singleton. The mod copies the D3D9 texture immediately on the game thread;
// engine pointers never cross to the panel thread.
constexpr uintptr_t kLocalMapTransformPointRva = 0x0033F200;
constexpr uintptr_t kLocalMapPrepareExteriorCellRva = 0x00345B80;
constexpr uintptr_t kLocalMapPrepareInteriorCellRva = 0x00344ED0;
constexpr uintptr_t kLocalMapExteriorTextureRva = 0x00344AF0;
constexpr uintptr_t kLocalMapInteriorTextureRva = 0x00345040;
constexpr uintptr_t kLocalMapFowCountRva = 0x00345B90;
constexpr uintptr_t kWorldSpaceCellLookupRva = 0x003CA2B0;
// MapMenu::UpdateQuestMarkers obtains the engine-owned quest-target list from
// PlayerCharacter and resolves each target through BGSQuestTarget::GetRef.
// Reusing these readers gives the isolated Local Map the same live objective
// references without creating or opening MapMenu.
constexpr uintptr_t kQuestTargetListGetterRva = 0x004B9170;
constexpr uintptr_t kQuestTargetReferenceGetterRva = 0x00376440;
constexpr uintptr_t kLocalMapLockGlobalRva = 0x00F256A8;
constexpr uintptr_t kNiDx9TextureDataRttiRva = 0x00F25354;
// MapMenu's exact renderer-data type check. This is validation evidence only;
// the mod never calls the global MapMenu chunk producer containing it.
constexpr uintptr_t kLocalMapTextureTypeOracleRva = 0x0054CE18;

constexpr uint8_t kPlayerPattern[] = {
    0x6A, 0x01, 0xFF, 0x75, 0xE4, 0x8B, 0xCB, 0x6A, 0x01,
    0x6A, 0x00, 0x6A, 0x01, 0xFF, 0x75, 0xEC, 0xE8, 0, 0, 0,
    0, 0x3B, 0x1D, 0, 0, 0, 0};
constexpr char kPlayerMask[] = "xxxxxxxxxxxxxxxxx????xx????";

// Vanilla UnequipItem's exact six-argument actor call.  This mirrors the
// EquipItem oracle above and proves both the native target and player global.
constexpr uint8_t kUnequipPattern[] = {
    0x6A, 0x01, 0xFF, 0x75, 0xEC, 0x8B, 0xCF, 0x6A, 0x01,
    0x6A, 0x00, 0x6A, 0x01, 0xFF, 0x75, 0xF0, 0xE8, 0, 0, 0,
    0, 0x3B, 0x3D, 0, 0, 0, 0};
constexpr char kUnequipMask[] = "xxxxxxxxxxxxxxxxx????xx????";

constexpr uint8_t kActorValuePattern[] = {
    0x8B, 0x46, 0x08, 0x57, 0x8B, 0x7D, 0x0C, 0xC1, 0xE8, 0x0B,
    0x57, 0xA8, 0x01, 0x75, 0, 0x8D, 0x8E, 0x9C, 0x00, 0x00,
    0x00, 0xEB, 0, 0x8B, 0x4E, 0x1C, 0x81, 0xC1, 0x00, 0x01,
    0x00, 0x00, 0x8B, 0x01, 0x8B, 0x40, 0x0C, 0xFF, 0xD0};
constexpr char kActorValueMask[] =
    "xxxxxxxxxxxxxx?xxxxxxx?xxxxxxxxxxxxxxxx";

constexpr uint8_t kContainerPattern[] = {
    0x6A, 0x15, 0xE8, 0, 0, 0, 0, 0x85, 0xC0, 0x74, 0x04,
    0x8B, 0x40, 0x0C, 0xC3, 0x33, 0xC0, 0xC3};
constexpr char kContainerMask[] = "xxx????xxxxxxxxxxx";

// InventoryEntryData::GetDisplayName for the exact installed 1.7.0.4 image.
// Absolute RTTI operands and the relative dynamic-cast call are relocation or
// link-layout dependent and are intentionally wildcarded. The surrounding
// control flow and EntryData+8 access are the structural oracle.
constexpr uint8_t kInventoryEntryNamePattern[] = {
    0x6A, 0x00, 0x68, 0, 0, 0, 0, 0x68, 0, 0, 0, 0,
    0x6A, 0x00, 0xFF, 0x71, 0x08, 0xE8, 0, 0, 0, 0,
    0x8B, 0xD0, 0x83, 0xC4, 0x14, 0x85, 0xD2, 0x74, 0x35,
    0x0F, 0xB7, 0x42, 0x08, 0xB9, 0xFF, 0xFF, 0x00, 0x00,
    0x66, 0x3B, 0xC1, 0x75, 0x13, 0x8B, 0x4A, 0x04};
constexpr char kInventoryEntryNameMask[] =
    "xxx????x????xxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxx";

// Exact 1.7.0.4 bootstrap sequence which allocates TESDataHandler (0x5B8),
// runs its constructor, publishes the singleton, then loads the data files.
constexpr uint8_t kDataHandlerPattern[] = {
    0x68, 0xB8, 0x05, 0x00, 0x00, 0xB9, 0, 0, 0, 0,
    0xE8, 0, 0, 0, 0, 0x89, 0x45, 0xE0, 0xC6, 0x45, 0xFC, 0x0E,
    0x85, 0xC0, 0x74, 0x09, 0x8B, 0xC8, 0xE8, 0, 0, 0, 0,
    0xEB, 0x02, 0x33, 0xC0, 0xFF, 0x75, 0x08, 0x8B, 0xC8,
    0xC6, 0x45, 0xFC, 0x08, 0xA3, 0, 0, 0, 0};
constexpr char kDataHandlerMask[] =
    "xxxxxx????x????xxxxxxxxxxxxxx????xxxxxxxxxxxxxx????";

static_assert(sizeof(kPlayerPattern) + 1 == sizeof(kPlayerMask));
static_assert(sizeof(kUnequipPattern) + 1 == sizeof(kUnequipMask));
static_assert(sizeof(kActorValuePattern) + 1 == sizeof(kActorValueMask));
static_assert(sizeof(kContainerPattern) + 1 == sizeof(kContainerMask));
static_assert(sizeof(kInventoryEntryNamePattern) + 1 ==
              sizeof(kInventoryEntryNameMask));
static_assert(sizeof(kDataHandlerPattern) + 1 == sizeof(kDataHandlerMask));

}  // namespace fo3ds::sig

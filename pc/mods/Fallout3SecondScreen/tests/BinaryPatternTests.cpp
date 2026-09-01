#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "Signatures.h"

namespace {

struct DiskCommandDescriptor {
  uint32_t long_name;
  uint32_t short_name;
  uint16_t opcode;
  uint16_t padding;
  uint32_t help;
  uint16_t needs_parent;
  uint16_t parameter_count;
  uint32_t parameters;
  uint32_t execute;
  uint32_t parse;
  uint32_t eval;
  uint32_t flags;
};

static_assert(sizeof(DiskCommandDescriptor) == 0x28);

struct Image {
  std::vector<uint8_t> bytes;
  const IMAGE_NT_HEADERS32* nt = nullptr;
  const IMAGE_SECTION_HEADER* sections = nullptr;

  const uint8_t* At(size_t offset, size_t size) const {
    return offset <= bytes.size() && size <= bytes.size() - offset
               ? bytes.data() + offset
               : nullptr;
  }

  size_t VaToOffset(uint32_t va) const {
    if (!nt || va < nt->OptionalHeader.ImageBase) return SIZE_MAX;
    const uint32_t rva = va - nt->OptionalHeader.ImageBase;
    for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
      const auto& section = sections[i];
      const uint32_t size = section.SizeOfRawData;
      if (rva >= section.VirtualAddress && rva - section.VirtualAddress < size)
        return section.PointerToRawData + (rva - section.VirtualAddress);
    }
    return rva < nt->OptionalHeader.SizeOfHeaders ? rva : SIZE_MAX;
  }
};

bool Load(const char* path, Image* image) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) return false;
  const std::streamsize size = stream.tellg();
  if (size <= 0) return false;
  image->bytes.resize(static_cast<size_t>(size));
  stream.seekg(0);
  if (!stream.read(reinterpret_cast<char*>(image->bytes.data()), size))
    return false;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(
      image->At(0, sizeof(IMAGE_DOS_HEADER)));
  if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
    return false;
  image->nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      image->At(static_cast<size_t>(dos->e_lfanew), sizeof(IMAGE_NT_HEADERS32)));
  if (!image->nt || image->nt->Signature != IMAGE_NT_SIGNATURE ||
      image->nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    return false;
  image->sections = IMAGE_FIRST_SECTION(image->nt);
  const size_t section_offset = reinterpret_cast<const uint8_t*>(image->sections) -
                                image->bytes.data();
  return image->At(section_offset, sizeof(IMAGE_SECTION_HEADER) *
                   image->nt->FileHeader.NumberOfSections) != nullptr;
}

bool SectionName(const IMAGE_SECTION_HEADER& section, const char* name) {
  char value[9] = {};
  std::memcpy(value, section.Name, 8);
  return std::strcmp(value, name) == 0;
}

std::vector<size_t> Find(const Image& image, const uint8_t* pattern,
                         const char* mask) {
  const IMAGE_SECTION_HEADER* text = nullptr;
  for (uint16_t i = 0; i < image.nt->FileHeader.NumberOfSections; ++i)
    if (SectionName(image.sections[i], ".text")) text = &image.sections[i];
  std::vector<size_t> matches;
  if (!text) return matches;
  const uint8_t* data = image.At(text->PointerToRawData, text->SizeOfRawData);
  const size_t length = std::strlen(mask);
  if (!data || !length || text->SizeOfRawData < length) return matches;
  for (size_t offset = 0; offset <= text->SizeOfRawData - length; ++offset) {
    bool equal = true;
    for (size_t i = 0; i < length; ++i) {
      if (mask[i] == 'x' && data[offset + i] != pattern[i]) {
        equal = false;
        break;
      }
    }
    if (equal) matches.push_back(text->PointerToRawData + offset);
  }
  return matches;
}

uint32_t OffsetToVa(const Image& image, size_t offset) {
  for (uint16_t i = 0; i < image.nt->FileHeader.NumberOfSections; ++i) {
    const auto& section = image.sections[i];
    if (offset >= section.PointerToRawData &&
        offset - section.PointerToRawData < section.SizeOfRawData)
      return image.nt->OptionalHeader.ImageBase + section.VirtualAddress +
             static_cast<uint32_t>(offset - section.PointerToRawData);
  }
  return 0;
}

bool Descriptor(const Image& image, uintptr_t rva, const char* name,
                uint16_t opcode, uint16_t parameters, uintptr_t execute_rva,
                uintptr_t eval_rva = 0) {
  const uint32_t va = image.nt->OptionalHeader.ImageBase + rva;
  const size_t offset = image.VaToOffset(va);
  const auto* descriptor = reinterpret_cast<const DiskCommandDescriptor*>(
      image.At(offset, sizeof(DiskCommandDescriptor)));
  if (!descriptor || descriptor->opcode != opcode ||
      descriptor->parameter_count != parameters ||
      descriptor->execute != image.nt->OptionalHeader.ImageBase + execute_rva ||
      (eval_rva && descriptor->eval !=
                       image.nt->OptionalHeader.ImageBase + eval_rva))
    return false;
  const size_t name_offset = image.VaToOffset(descriptor->long_name);
  const char* actual = reinterpret_cast<const char*>(
      image.At(name_offset, std::strlen(name) + 1));
  return actual && std::strcmp(actual, name) == 0;
}

bool TypeDescriptor(const Image& image, uintptr_t rva, const char* name) {
  const IMAGE_SECTION_HEADER* data = nullptr;
  for (uint16_t i = 0; i < image.nt->FileHeader.NumberOfSections; ++i)
    if (SectionName(image.sections[i], ".data")) data = &image.sections[i];
  const size_t descriptor_size = 8 + std::strlen(name) + 1;
  if (!data || rva < data->VirtualAddress ||
      rva - data->VirtualAddress > data->Misc.VirtualSize ||
      descriptor_size >
          data->Misc.VirtualSize - (rva - data->VirtualAddress))
    return false;
  const uint32_t va = image.nt->OptionalHeader.ImageBase + rva + 8;
  const size_t offset = image.VaToOffset(va);
  const char* actual = reinterpret_cast<const char*>(
      image.At(offset, std::strlen(name) + 1));
  return actual && std::strcmp(actual, name) == 0;
}

bool ImportNameAtIatRva(const Image& image, uint32_t iat_rva,
                        const char* expected_dll,
                        const char* expected_name) {
  const IMAGE_DATA_DIRECTORY& directory =
      image.nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!directory.VirtualAddress || !directory.Size) return false;
  const size_t table_offset = image.VaToOffset(
      image.nt->OptionalHeader.ImageBase + directory.VirtualAddress);
  for (size_t index = 0;; ++index) {
    const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        image.At(table_offset + index * sizeof(IMAGE_IMPORT_DESCRIPTOR),
                 sizeof(IMAGE_IMPORT_DESCRIPTOR)));
    if (!descriptor || !descriptor->Name) return false;
    const size_t dll_offset = image.VaToOffset(
        image.nt->OptionalHeader.ImageBase + descriptor->Name);
    const char* dll = reinterpret_cast<const char*>(
        image.At(dll_offset, std::strlen(expected_dll) + 1));
    if (!dll) return false;
    if (_stricmp(dll, expected_dll) != 0) continue;
    const uint32_t first_thunk = descriptor->FirstThunk;
    if (iat_rva < first_thunk || (iat_rva - first_thunk) % 4 != 0) continue;
    const uint32_t thunk_index = (iat_rva - first_thunk) / 4;
    const uint32_t lookup_rva = descriptor->OriginalFirstThunk
        ? descriptor->OriginalFirstThunk : descriptor->FirstThunk;
    const size_t thunk_offset = image.VaToOffset(
        image.nt->OptionalHeader.ImageBase + lookup_rva + thunk_index * 4);
    const auto* thunk = reinterpret_cast<const IMAGE_THUNK_DATA32*>(
        image.At(thunk_offset, sizeof(IMAGE_THUNK_DATA32)));
    if (!thunk || IMAGE_SNAP_BY_ORDINAL32(thunk->u1.Ordinal)) return false;
    const size_t name_offset = image.VaToOffset(
        image.nt->OptionalHeader.ImageBase + thunk->u1.AddressOfData);
    const auto* imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
        image.At(name_offset, sizeof(WORD) + std::strlen(expected_name) + 1));
    return imported &&
           std::strcmp(reinterpret_cast<const char*>(imported->Name),
                       expected_name) == 0;
  }
}

void Check(bool condition, const char* message, int* failures) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++*failures;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: BinaryPatternTests <Fallout3.exe>\n");
    return 2;
  }
  Image image;
  if (!Load(argv[1], &image)) {
    std::fprintf(stderr, "could not parse target executable\n");
    return 2;
  }
  int failures = 0;
  Check(image.bytes.size() == 16855040, "exact target file size", &failures);
  Check(image.nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386,
        "target is i386", &failures);
  Check(image.nt->FileHeader.TimeDateStamp == 0x60A80E56,
        "target timestamp", &failures);
  Check(Descriptor(image, fo3ds::sig::kGetActorValueDescriptorRva,
                   "GetActorValue", 0x100E, 1,
                   fo3ds::sig::kGetActorValueExecuteRva,
                   fo3ds::sig::kGetActorValueEvalRva),
        "GetActorValue descriptor", &failures);
  Check(Descriptor(image, fo3ds::sig::kGetStageDescriptorRva, "GetStage",
                   0x103A, 1, fo3ds::sig::kGetStageExecuteRva,
                   fo3ds::sig::kGetStageEvalRva),
        "GetStage descriptor", &failures);
  Check(Descriptor(image, fo3ds::sig::kEquipDescriptorRva, "EquipItem",
                   0x10EE, 3, fo3ds::sig::kEquipExecuteRva),
        "EquipItem descriptor", &failures);
  Check(Descriptor(image, fo3ds::sig::kUnequipDescriptorRva, "UnequipItem",
                   0x10EF, 3, fo3ds::sig::kUnequipExecuteRva),
        "UnequipItem descriptor", &failures);
  Check(Descriptor(image, fo3ds::sig::kHasPerkDescriptorRva, "HasPerk",
                   0x11C1, 1, fo3ds::sig::kHasPerkExecuteRva,
                   fo3ds::sig::kHasPerkEvalRva),
        "HasPerk descriptor", &failures);
  Check(Descriptor(image, fo3ds::sig::kGetXpForNextLevelDescriptorRva,
                   "GetXPForNextLevel", 0x1215, 0,
                   fo3ds::sig::kGetXpForNextLevelExecuteRva,
                   fo3ds::sig::kGetXpForNextLevelEvalRva),
        "GetXPForNextLevel descriptor", &failures);
  Check(Descriptor(image, fo3ds::sig::kGetPcMiscStatDescriptorRva,
                   "GetPCMiscStat", 0x1138, 1,
                   fo3ds::sig::kGetPcMiscStatExecuteRva,
                   fo3ds::sig::kGetPcMiscStatEvalRva),
        "GetPCMiscStat descriptor", &failures);
  Check(Descriptor(image, fo3ds::sig::kPipboyRadioDescriptorRva,
                   "PipboyRadio", 0x11A7, 2,
                   fo3ds::sig::kPipboyRadioExecuteRva),
        "PipboyRadio descriptor", &failures);
  Check(Descriptor(image, fo3ds::sig::kPipboyRadioOffDescriptorRva,
                   "PipBoyRadioOff", 0x1224, 0,
                   fo3ds::sig::kPipboyRadioOffExecuteRva),
        "PipBoyRadioOff descriptor", &failures);

  const auto exact_bytes = [&](uintptr_t rva, const uint8_t* expected,
                               size_t count) {
    const size_t offset = image.VaToOffset(
        image.nt->OptionalHeader.ImageBase + static_cast<uint32_t>(rva));
    const uint8_t* actual = image.At(offset, count);
    return actual && std::memcmp(actual, expected, count) == 0;
  };
  const uint8_t hud_health_timer_context[] = {
      0xFF, 0x15, 0x58, 0x40, 0xF2, 0x00, 0x50, 0x8D, 0x8F,
      0xF4, 0x01, 0x00, 0x00, 0xE8, 0x1B, 0xCC, 0x0A, 0x00};
  const uint8_t changed_health_timer_context[] = {
      0xFF, 0x15, 0x58, 0x40, 0xF2, 0x00, 0x50, 0x8D, 0x8E,
      0xF4, 0x01, 0x00, 0x00, 0xE8, 0x7C, 0x9C, 0x0A, 0x00};
  Check(exact_bytes(0x0052DA40 - 13, hud_health_timer_context,
                    sizeof(hud_health_timer_context)),
        "HUD health timer call context", &failures);
  Check(exact_bytes(0x005309DF - 13, changed_health_timer_context,
                    sizeof(changed_health_timer_context)),
        "target-health-change timer call context", &failures);
  const uint8_t map_menu_interior_anchor[] = {
      0xA1, 0x74, 0xC6, 0x23, 0x01, 0x8B, 0x80, 0x88, 0x05,
      0x00, 0x00, 0x89, 0x87, 0x04, 0x01, 0x00, 0x00};
  Check(exact_bytes(0x00545FD7, map_menu_interior_anchor,
                    sizeof(map_menu_interior_anchor)),
        "MapMenu uses PlayerCharacter+0x588 for its interior anchor",
        &failures);
  const uint8_t local_marker_reference_oracle[] = {
      0x8B, 0x47, 0x1C, 0x33, 0xDB, 0x8B, 0xCF, 0x80, 0x78, 0x04,
      0x1C, 0x0F, 0x44, 0xD8, 0x8B, 0x07, 0xFF, 0x90, 0xF4, 0x01,
      0x00, 0x00};
  Check(exact_bytes(0x0054F33A, local_marker_reference_oracle,
                    sizeof(local_marker_reference_oracle)),
        "MapMenu local markers require door records and virtual positions",
        &failures);
  const uint8_t local_player_position_oracle[] = {
      0x8B, 0x0D, 0x74, 0xC6, 0x23, 0x01, 0x89, 0x45, 0xE8,
      0x8B, 0x11, 0x8B, 0x59, 0x3C, 0xFF, 0x92, 0xF4, 0x01,
      0x00, 0x00};
  Check(exact_bytes(0x0054F175, local_player_position_oracle,
                    sizeof(local_player_position_oracle)),
        "MapMenu local raster anchors through the player virtual position",
        &failures);
  Check(ImportNameAtIatRva(image, 0x00B2407C, "KERNEL32.dll",
                           "WaitForSingleObject"),
        "WaitForSingleObject IAT slot", &failures);
  Check(ImportNameAtIatRva(image, 0x00B24100, "KERNEL32.dll",
                           "WaitForMultipleObjects"),
        "WaitForMultipleObjects IAT slot", &failures);
  Check(ImportNameAtIatRva(image, 0x00B2417C, "KERNEL32.dll",
                           "WaitForSingleObjectEx"),
        "WaitForSingleObjectEx IAT slot", &failures);
  Check(ImportNameAtIatRva(image, 0x00B241E8, "KERNEL32.dll", "Sleep"),
        "Sleep IAT slot", &failures);
  const uint8_t radio_enable_prologue[] = {0x55, 0x8B, 0xEC, 0x80, 0x3D};
  const uint8_t xp_next_eval_prologue[] = {0x55, 0x8B, 0xEC, 0x53, 0x56,
                                           0x57, 0x8B, 0x7D, 0x14};
  const uint8_t active_effect_list_getter[] = {0x8B, 0x81, 0x40, 0x01,
                                               0x00, 0x00, 0xC3};
  const uint8_t active_quest_setter[] = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08,
                                         0x3B, 0x81, 0x14, 0x06, 0x00, 0x00};
  const uint8_t quest_log_text_resolver[] = {0x55, 0x8B, 0xEC, 0x83,
                                             0xEC, 0x10, 0xA1};
  const uint8_t map_marker_list_builder[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC,
                                             0x08, 0x56, 0x8B, 0xF1};
  const uint8_t map_marker_data_getter[] = {0x83, 0xC1, 0x40, 0xE9};
  const uint8_t world_map_data_resolver[] = {
      0x8B, 0x41, 0x70, 0x85, 0xC0, 0x74, 0x18, 0xF6,
      0x41, 0x4E, 0x04, 0x74, 0x12};
  const uint8_t world_map_texture_resolver[] = {
      0x8B, 0x41, 0x70, 0x85, 0xC0, 0x74, 0x14, 0xF6,
      0x41, 0x4E, 0x04, 0x74, 0x0E};
  const uint8_t local_map_transform[] = {
      0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x80, 0x7D, 0x10, 0x00};
  const uint8_t local_map_exterior_texture[] = {
      0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x55, 0xA2, 0xDE, 0x00};
  const uint8_t local_map_prepare_interior[] = {
      0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xA3, 0xA2, 0xDE, 0x00};
  const uint8_t local_map_interior_texture[] = {
      0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x55, 0xA2, 0xDE, 0x00};
  const uint8_t local_map_prepare_exterior[] = {
      0x83, 0xC1, 0x28, 0xE9};
  const uint8_t local_map_fow_count[] = {
      0x55, 0x8B, 0xEC, 0xA1, 0x74, 0xC6, 0x23, 0x01};
  const uint8_t worldspace_cell_lookup[] = {
      0x55, 0x8B, 0xEC, 0x8B, 0x55, 0x08, 0x56, 0x8B, 0xF1};
  const uint8_t quest_target_list[] = {
      0x56, 0x8B, 0xF1, 0x8B, 0x8E, 0x14, 0x06, 0x00,
      0x00, 0x85, 0xC9, 0x75, 0x04, 0x33, 0xC0, 0x5E, 0xC3};
  const uint8_t quest_target_reference[] = {
      0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x71, 0x0C, 0x85,
      0xF6, 0x74, 0x1D, 0x8B, 0x46, 0x08, 0xC1, 0xE8,
      0x05, 0xA8, 0x01};
  const uint8_t local_map_texture_type_oracle[] = {
      0x3D, 0x54, 0x53, 0x32, 0x01, 0x74, 0x3B, 0x8B,
      0x40, 0x04, 0x85, 0xC0, 0x75, 0xF2};
  const uint8_t simple_list_destructor[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};
  const uint8_t note_image_accessor[] = {
      0x80, 0x79, 0x7C, 0x02, 0x75, 0x04, 0x8B, 0x41, 0x6C, 0xC3,
      0x33, 0xC0, 0xC3};
  const uint8_t note_text_accessor[] = {
      0x80, 0x79, 0x7C, 0x01, 0x75, 0x04, 0x8B, 0x41, 0x6C, 0xC3,
      0x33, 0xC0, 0xC3};
  const uint8_t radio_tune_prologue[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};
  const uint8_t radio_builder_prologue[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};
  const uint8_t list_destructor_prologue[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};
  const uint8_t heap_free_prologue[] = {0x56, 0x8B, 0x74, 0x24, 0x08};
  Check(exact_bytes(fo3ds::sig::kRadioEnableRva, radio_enable_prologue,
                    sizeof(radio_enable_prologue)),
        "radio enable native", &failures);
  Check(exact_bytes(fo3ds::sig::kGetXpForNextLevelEvalRva,
                    xp_next_eval_prologue, sizeof(xp_next_eval_prologue)),
        "GetXPForNextLevel evaluator", &failures);
  Check(exact_bytes(fo3ds::sig::kGetActiveEffectListRva,
                    active_effect_list_getter,
                    sizeof(active_effect_list_getter)),
        "MagicTarget active-effect-list getter", &failures);
  Check(exact_bytes(fo3ds::sig::kActiveQuestSetterRva,
                    active_quest_setter, sizeof(active_quest_setter)),
        "PlayerCharacter active-quest setter", &failures);
  Check(exact_bytes(fo3ds::sig::kQuestLogTextResolverRva,
                    quest_log_text_resolver,
                    sizeof(quest_log_text_resolver)),
        "quest journal-text resolver", &failures);
  const size_t quest_log_call_offset = image.VaToOffset(
      image.nt->OptionalHeader.ImageBase +
      fo3ds::sig::kShowQuestLogTextCallRva);
  const uint8_t* quest_log_call = image.At(quest_log_call_offset, 5);
  int32_t quest_log_displacement = 0;
  if (quest_log_call)
    std::memcpy(&quest_log_displacement, quest_log_call + 1,
                sizeof(quest_log_displacement));
  Check(quest_log_call && quest_log_call[0] == 0xE8 &&
            image.nt->OptionalHeader.ImageBase +
                    fo3ds::sig::kShowQuestLogTextCallRva + 5 +
                    quest_log_displacement ==
                image.nt->OptionalHeader.ImageBase +
                    fo3ds::sig::kQuestLogTextResolverRva,
        "ShowQuestLog journal-text callsite", &failures);
  Check(exact_bytes(fo3ds::sig::kWorldMapMarkerListBuilderRva,
                    map_marker_list_builder,
                    sizeof(map_marker_list_builder)),
        "world map-marker list builder", &failures);
  Check(exact_bytes(fo3ds::sig::kMapMarkerDataGetterRva,
                    map_marker_data_getter,
                    sizeof(map_marker_data_getter)),
        "map-marker extra-data getter", &failures);
  Check(exact_bytes(fo3ds::sig::kWorldMapDataResolverRva,
                    world_map_data_resolver,
                    sizeof(world_map_data_resolver)),
        "TESWorldSpace effective MNAM resolver", &failures);
  Check(exact_bytes(fo3ds::sig::kWorldMapTextureResolverRva,
                    world_map_texture_resolver,
                    sizeof(world_map_texture_resolver)),
        "TESWorldSpace effective map-texture resolver", &failures);
  Check(exact_bytes(fo3ds::sig::kLocalMapTransformPointRva,
                    local_map_transform, sizeof(local_map_transform)),
        "local-map interior coordinate transform", &failures);
  Check(exact_bytes(fo3ds::sig::kLocalMapExteriorTextureRva,
                    local_map_exterior_texture,
                    sizeof(local_map_exterior_texture)),
        "local-map exterior texture getter", &failures);
  Check(exact_bytes(fo3ds::sig::kLocalMapPrepareInteriorCellRva,
                    local_map_prepare_interior,
                    sizeof(local_map_prepare_interior)),
        "local-map interior cell preparation", &failures);
  Check(exact_bytes(fo3ds::sig::kLocalMapInteriorTextureRva,
                    local_map_interior_texture,
                    sizeof(local_map_interior_texture)),
        "local-map interior texture getter", &failures);
  Check(exact_bytes(fo3ds::sig::kLocalMapPrepareExteriorCellRva,
                    local_map_prepare_exterior,
                    sizeof(local_map_prepare_exterior)),
        "local-map exterior cell preparation", &failures);
  Check(exact_bytes(fo3ds::sig::kLocalMapFowCountRva,
                    local_map_fow_count, sizeof(local_map_fow_count)),
        "local-map fog-of-war sampler", &failures);
  Check(exact_bytes(fo3ds::sig::kWorldSpaceCellLookupRva,
                    worldspace_cell_lookup,
                    sizeof(worldspace_cell_lookup)),
        "worldspace local-map cell lookup", &failures);
  Check(exact_bytes(fo3ds::sig::kQuestTargetListGetterRva,
                    quest_target_list, sizeof(quest_target_list)),
        "PlayerCharacter native quest-target list", &failures);
  Check(exact_bytes(fo3ds::sig::kQuestTargetReferenceGetterRva,
                    quest_target_reference,
                    sizeof(quest_target_reference)),
        "native quest-target reference resolver", &failures);
  Check(exact_bytes(fo3ds::sig::kLocalMapTextureTypeOracleRva,
                    local_map_texture_type_oracle,
                    sizeof(local_map_texture_type_oracle)),
        "MapMenu NiDX9 texture-data type oracle", &failures);
  Check(exact_bytes(fo3ds::sig::kSimpleListDestructorRva,
                    simple_list_destructor,
                    sizeof(simple_list_destructor)),
        "temporary simple-list destructor", &failures);
  Check(exact_bytes(fo3ds::sig::kNoteImagePayloadGetterRva,
                    note_image_accessor, sizeof(note_image_accessor)),
        "BGSNote image-payload accessor", &failures);
  Check(exact_bytes(fo3ds::sig::kNoteTextPayloadGetterRva,
                    note_text_accessor, sizeof(note_text_accessor)),
        "BGSNote text-payload accessor", &failures);
  const size_t magic_target_vtable_offset = image.VaToOffset(
      image.nt->OptionalHeader.ImageBase +
      fo3ds::sig::kPlayerMagicTargetVtableRva);
  const uint32_t* magic_target_vtable =
      reinterpret_cast<const uint32_t*>(image.At(magic_target_vtable_offset,
                                                 3 * sizeof(uint32_t)));
  Check(magic_target_vtable && magic_target_vtable[2] ==
            image.nt->OptionalHeader.ImageBase +
                fo3ds::sig::kGetActiveEffectListRva,
        "PlayerCharacter MagicTarget vtable", &failures);
  Check(exact_bytes(fo3ds::sig::kRadioTuneRva, radio_tune_prologue,
                    sizeof(radio_tune_prologue)),
        "radio tune native", &failures);
  Check(exact_bytes(fo3ds::sig::kRadioCandidateBuilderRva,
                    radio_builder_prologue,
                    sizeof(radio_builder_prologue)),
        "radio candidate builder", &failures);
  Check(exact_bytes(fo3ds::sig::kListNodeDestructorRva,
                    list_destructor_prologue,
                    sizeof(list_destructor_prologue)),
        "temporary list-node destructor", &failures);
  Check(exact_bytes(fo3ds::sig::kHeapFreeRva, heap_free_prologue,
                    sizeof(heap_free_prologue)),
        "engine heap free", &failures);
  Check(TypeDescriptor(image, fo3ds::sig::kTesFormTypeDescriptorRva,
                       ".?AVTESForm@@"),
        "TESForm RTTI descriptor", &failures);
  Check(TypeDescriptor(image, fo3ds::sig::kTesIconTypeDescriptorRva,
                       ".?AVTESIcon@@"),
        "TESIcon RTTI descriptor", &failures);
  Check(TypeDescriptor(image, fo3ds::sig::kTesWeightFormTypeDescriptorRva,
                       ".?AVTESWeightForm@@"),
        "TESWeightForm RTTI descriptor", &failures);
  Check(TypeDescriptor(image, fo3ds::sig::kTesValueFormTypeDescriptorRva,
                       ".?AVTESValueForm@@"),
        "TESValueForm RTTI descriptor", &failures);
  Check(TypeDescriptor(image, fo3ds::sig::kTesHealthFormTypeDescriptorRva,
                       ".?AVTESHealthForm@@"),
        "TESHealthForm RTTI descriptor", &failures);
  Check(TypeDescriptor(
            image, fo3ds::sig::kTesAttackDamageFormTypeDescriptorRva,
            ".?AVTESAttackDamageForm@@"),
        "TESAttackDamageForm RTTI descriptor", &failures);
  Check(TypeDescriptor(image, fo3ds::sig::kTesObjectArmoTypeDescriptorRva,
                       ".?AVTESObjectARMO@@"),
        "TESObjectARMO RTTI descriptor", &failures);
  Check(TypeDescriptor(image, fo3ds::sig::kTesQuestTypeDescriptorRva,
                       ".?AVTESQuest@@"),
        "TESQuest RTTI descriptor", &failures);
  Check(TypeDescriptor(image, fo3ds::sig::kBgsNoteTypeDescriptorRva,
                       ".?AVBGSNote@@"),
        "BGSNote RTTI descriptor", &failures);
  Check(TypeDescriptor(image, fo3ds::sig::kBgsPerkTypeDescriptorRva,
                       ".?AVBGSPerk@@"),
        "BGSPerk RTTI descriptor", &failures);
  Check(TypeDescriptor(image, fo3ds::sig::kTesWorldSpaceTypeDescriptorRva,
                       ".?AVTESWorldSpace@@"),
        "TESWorldSpace RTTI descriptor", &failures);
  Check(TypeDescriptor(image, fo3ds::sig::kTesObjectCellTypeDescriptorRva,
                       ".?AVTESObjectCELL@@"),
        "TESObjectCELL RTTI descriptor", &failures);
  Check(TypeDescriptor(
            image, fo3ds::sig::kBgsQuestObjectiveTypeDescriptorRva,
            ".?AVBGSQuestObjective@@"),
        "BGSQuestObjective RTTI descriptor", &failures);

  const auto player = Find(image, fo3ds::sig::kPlayerPattern,
                           fo3ds::sig::kPlayerMask);
  Check(player.size() == 1, "player/equip signature is unique", &failures);
  if (player.size() == 1) {
    const uint32_t match_va = OffsetToVa(image, player[0]);
    int32_t displacement = 0;
    uint32_t singleton = 0;
    std::memcpy(&displacement, image.At(player[0] + 17, 4), 4);
    std::memcpy(&singleton, image.At(player[0] + 23, 4), 4);
    Check(match_va >= 0x00400000 + fo3ds::sig::kEquipExecuteRva &&
              match_va < 0x00400000 + fo3ds::sig::kEquipExecuteRva + 0x500,
          "player signature belongs to EquipItem execute", &failures);
    Check(match_va + 21 + displacement ==
              0x00400000 + fo3ds::sig::kEquipNativeRva,
          "player signature decodes equip native", &failures);
    Check(singleton == 0x00400000 + fo3ds::sig::kPlayerGlobalRva,
          "player signature decodes singleton", &failures);
  }

  const auto unequip = Find(image, fo3ds::sig::kUnequipPattern,
                            fo3ds::sig::kUnequipMask);
  Check(unequip.size() == 1, "player/unequip signature is unique", &failures);
  if (unequip.size() == 1) {
    const uint32_t match_va = OffsetToVa(image, unequip[0]);
    int32_t displacement = 0;
    uint32_t singleton = 0;
    std::memcpy(&displacement, image.At(unequip[0] + 17, 4), 4);
    std::memcpy(&singleton, image.At(unequip[0] + 23, 4), 4);
    Check(match_va >= 0x00400000 + fo3ds::sig::kUnequipExecuteRva &&
              match_va < 0x00400000 + fo3ds::sig::kUnequipExecuteRva + 0x500,
          "unequip signature belongs to UnequipItem execute", &failures);
    Check(match_va + 21 + displacement ==
              0x00400000 + fo3ds::sig::kUnequipNativeRva,
          "unequip signature decodes native target", &failures);
    Check(singleton == 0x00400000 + fo3ds::sig::kPlayerGlobalRva,
          "unequip signature decodes singleton", &failures);
  }

  const auto actor = Find(image, fo3ds::sig::kActorValuePattern,
                          fo3ds::sig::kActorValueMask);
  Check(actor.size() == 1, "actor-value signature is unique", &failures);
  if (actor.size() == 1) {
    const uint32_t va = OffsetToVa(image, actor[0]);
    Check(va >= 0x00400000 + fo3ds::sig::kGetActorValueEvalRva &&
              va < 0x00400000 + fo3ds::sig::kGetActorValueEvalRva + 0x500,
          "actor-value signature belongs to eval function", &failures);
  }

  const auto container = Find(image, fo3ds::sig::kContainerPattern,
                              fo3ds::sig::kContainerMask);
  Check(container.size() == 1, "container getter signature is unique", &failures);
  if (container.size() == 1)
    Check(OffsetToVa(image, container[0]) ==
              0x00400000 + fo3ds::sig::kContainerChangesGetterRva,
          "container getter address", &failures);

  const auto inventory_name = Find(
      image, fo3ds::sig::kInventoryEntryNamePattern,
      fo3ds::sig::kInventoryEntryNameMask);
  Check(inventory_name.size() == 1,
        "inventory display-name signature is unique", &failures);
  if (inventory_name.size() == 1)
    Check(OffsetToVa(image, inventory_name[0]) ==
              0x00400000 + fo3ds::sig::kInventoryEntryNameRva,
          "inventory display-name helper address", &failures);

  const auto data_handler = Find(image, fo3ds::sig::kDataHandlerPattern,
                                 fo3ds::sig::kDataHandlerMask);
  Check(data_handler.size() == 1,
        "TESDataHandler constructor signature is unique", &failures);
  if (data_handler.size() == 1) {
    uint32_t singleton = 0;
    std::memcpy(&singleton, image.At(data_handler[0] + 47, 4), 4);
    Check(OffsetToVa(image, data_handler[0]) == 0x006DC3A1,
          "TESDataHandler constructor site", &failures);
    Check(singleton ==
              0x00400000 + fo3ds::sig::kDataHandlerGlobalRva,
          "TESDataHandler signature decodes singleton", &failures);
  }

  if (failures) {
    std::fprintf(stderr, "%d exact-binary test(s) failed\n", failures);
    return 1;
  }
  std::puts("Exact Fallout 3 binary signature tests passed");
  return 0;
}

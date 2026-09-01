#include "GameplayBridge.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Game.h"
#include "Log.h"
#include "Mem.h"
#include "Rtti.h"
#include "SkillCatalog.h"

namespace dis {
namespace gameplay {
namespace {

constexpr uint32_t kGameTimestamp = 0x5745B64C;
constexpr uint32_t kGameImageSize = 0x014C8000;

constexpr uintptr_t kItemNameRva = 0x003954C0;
constexpr uintptr_t kLocalizationManagerRva = 0x0127B388;
constexpr uintptr_t kEquipOrUseRva = 0x00674D30;
constexpr uintptr_t kAssignQuickSlotRva = 0x00689FC0;
constexpr uintptr_t kSkillRankRva = 0x006D6F90;
constexpr uintptr_t kSkillRegistryRva = 0x0010EC40;
constexpr uintptr_t kSkillLookupRva = 0x00379720;
constexpr uintptr_t kSkillCanBuyRva = 0x006D7830;
constexpr uintptr_t kSkillPurchaseByNameRva = 0x006D7540;
constexpr uintptr_t kMenuInventoryPlayerLoadRva = 0x00455EC8;
constexpr uintptr_t kDefaultProgressionOwnerLoadRva = 0x00548B4D;
constexpr uintptr_t kVariableOwnerSelectRva = 0x00548B74;
constexpr uintptr_t kCharacterEnumLoadRva = 0x00690C63;
constexpr uintptr_t kGameplayRootRva = 0x0127C2F8;
constexpr uintptr_t kDefaultProgressionOwnerRva = 0x0127BB88;

constexpr size_t kLogicalPlayerOffset = 0x0A18;
constexpr size_t kGameplayRootPlayerOffset = 0x0628;
constexpr size_t kVariableSourceFlagOffset = 0x0050;
constexpr size_t kPrimaryHandHandleOffset = 0x0FE8;
constexpr size_t kPrimaryHandItemOffset = 0x0FF0;
constexpr size_t kSecondaryHandHandleOffset = 0x1008;
constexpr size_t kSecondaryHandItemOffset = 0x1010;
constexpr size_t kInventoryArrayOffset = 0x1088;
constexpr size_t kInventoryCountOffset = 0x1090;
constexpr size_t kInventoryCapacityOffset = 0x1094;
constexpr size_t kQuickArrayOffset = 0x10A0;
constexpr size_t kQuickCountOffset = 0x10A8;
constexpr size_t kQuickCapacityOffset = 0x10AC;
constexpr size_t kExtraArrayOffset = 0x10E8;
constexpr size_t kExtraCountOffset = 0x10F0;
constexpr size_t kExtraCapacityOffset = 0x10F4;
// The playable-character enum read by the game's own skill/menu callers.
constexpr size_t kCharacterEnumOffset = 0x1B5C;
constexpr size_t kItemCountOffset = 0x50;
constexpr size_t kItemDescriptorOffset = 0x60;

constexpr uint8_t kItemNameSignature[] = {
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
};
constexpr uint8_t kEquipOrUseSignature[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
};
// PlayerDI's native MenuInventory assignment primitive. The menu passes the
// live PlayerDI, its selected owned item and the zero-based raw quick-array
// index. Return value 0 means success; 1/2/3 are the game's own range,
// ownership and eligibility rejections respectively.
constexpr uint8_t kAssignQuickSlotSignature[] = {
    0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x7C,
    0x24, 0x20, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x20,
};
constexpr uint8_t kSkillRankSignature[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30,
};
constexpr uint8_t kSkillCanBuySignature[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
    0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
};
constexpr uint8_t kSkillPurchaseSignature[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
};
constexpr uint8_t kSkillRegistrySignature[] = {
    0x48, 0x83, 0xEC, 0x28, 0x8B, 0x05, 0x06, 0x1C,
    0x21, 0x01, 0xA8, 0x01, 0x75, 0x5D,
};
constexpr uint8_t kSkillLookupSignature[] = {
    0x4C, 0x8B, 0x01, 0x45, 0x33, 0xC9, 0x4C, 0x8B,
    0xD2, 0x4D, 0x85, 0xC0,
};
// MenuInventory obtains the authoritative local PlayerDI through
//   gameplay root -> +0x628
// rather than from the IControlObject actor returned by the level scan.
constexpr uint8_t kMenuInventoryPlayerLoadSignature[] = {
    0x48, 0x8B, 0x05, 0x29, 0x64, 0xE2, 0x00,
    0x48, 0x85, 0xC0,
    0x0F, 0x84, 0x29, 0x1A, 0x00, 0x00,
    0x4D, 0x89, 0x6B, 0xD0,
    0x4C, 0x8B, 0xA8, 0x28, 0x06, 0x00, 0x00,
};
// Player-variable and skill code chooses the global progression owner when
// PlayerDI+0x50 is non-zero, otherwise it follows PlayerDI+0xA18.
constexpr uint8_t kVariableOwnerSelectSignature[] = {
    0x45, 0x84, 0xD2, 0x75, 0x07,
    0x49, 0x8B, 0x80, 0x18, 0x0A, 0x00, 0x00,
    0x48, 0x8B, 0x80, 0xB0, 0x00, 0x00, 0x00,
};
constexpr uint8_t kDefaultProgressionOwnerLoadSignature[] = {
    0x4C, 0x8B, 0x0D, 0x34, 0x30, 0xD3, 0x00,
};
constexpr uint8_t kCharacterEnumLoadSignature[] = {
    0x41, 0x8B, 0x94, 0x24, 0x5C, 0x1B, 0x00, 0x00,
    0x48, 0x8D, 0x4D, 0xF0,
};

struct CompleteObjectLocator64 {
  uint32_t signature;
  uint32_t subobject_offset;
  uint32_t constructor_displacement;
  int32_t type_descriptor_rva;
  int32_t class_descriptor_rva;
  int32_t self_rva;
};

struct TtlString {
  const char* data = nullptr;
  uint32_t size = 0;
  uint32_t capacity = 0;
};
static_assert(sizeof(TtlString) == 16, "ttl string ABI changed");

// Localization accepts an 8-bit key but returns ttl::string_base<wchar_t>.
// Treating that result as ttl::string_base<char> leaves an embedded NUL after
// every ASCII character. GDI then renders the remaining bytes as missing-glyph
// boxes, which is the square/cube text seen on inventory and equipped items.
struct TtlWideString {
  const wchar_t* data = nullptr;
  uint32_t size = 0;
  uint32_t capacity = 0;
};
static_assert(sizeof(TtlWideString) == 16, "wide ttl string ABI changed");

struct Build {
  uint8_t* image = nullptr;
  bool valid = false;
};

struct ItemMeta {
  std::string key;
  std::string name;
  int category = -1;
  int type = -1;
  int required_level = -1;
  uint64_t descriptor_identity = 0;
};

struct PlayerContext {
  uint8_t* complete = nullptr;
  uint8_t* logical = nullptr;
  bool uses_default_progression = false;
};

struct BridgeState {
  void* gameplay_root = nullptr;
  uint8_t* player = nullptr;
  uint8_t* logical = nullptr;
  std::vector<void*> inventory_items;
  std::vector<void*> quick_items;
  void* selected_item = nullptr;
  std::vector<std::string> skill_ids;
  std::string selected_skill;
  std::string character;
  std::unordered_map<void*, ItemMeta> item_cache;
  bool inventory_logged = false;
  bool containers_logged = false;
  bool skills_logged = false;
  bool uses_default_progression = false;
};

BridgeState g_state;

bool MatchBytes(const uint8_t* address, const uint8_t* expected, size_t size) {
  std::vector<uint8_t> actual(size);
  return address && expected && size != 0 &&
         Readable(address, size) &&
         std::memcpy(actual.data(), address, size) &&
         std::memcmp(actual.data(), expected, size) == 0;
}

Build VerifiedBuild() {
  static int cached = -1;
  static uint8_t* cached_image = nullptr;
  if (cached >= 0) return {cached_image, cached != 0};

  HMODULE module = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
  if (!module) return {};
  auto* image = reinterpret_cast<uint8_t*>(module);
  IMAGE_DOS_HEADER dos = {};
  IMAGE_NT_HEADERS64 nt = {};
  const bool valid =
      Read(image, &dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
      dos.e_lfanew > 0 && Read(image + dos.e_lfanew, &nt) &&
      nt.Signature == IMAGE_NT_SIGNATURE &&
      nt.FileHeader.TimeDateStamp == kGameTimestamp &&
      nt.OptionalHeader.SizeOfImage == kGameImageSize &&
      MatchBytes(image + kItemNameRva, kItemNameSignature,
                 sizeof(kItemNameSignature)) &&
      MatchBytes(image + kEquipOrUseRva, kEquipOrUseSignature,
                 sizeof(kEquipOrUseSignature)) &&
      MatchBytes(image + kAssignQuickSlotRva, kAssignQuickSlotSignature,
                 sizeof(kAssignQuickSlotSignature)) &&
      MatchBytes(image + kSkillRankRva, kSkillRankSignature,
                 sizeof(kSkillRankSignature)) &&
      MatchBytes(image + kSkillCanBuyRva, kSkillCanBuySignature,
                 sizeof(kSkillCanBuySignature)) &&
      MatchBytes(image + kSkillPurchaseByNameRva, kSkillPurchaseSignature,
                 sizeof(kSkillPurchaseSignature)) &&
      MatchBytes(image + kSkillRegistryRva, kSkillRegistrySignature,
                  sizeof(kSkillRegistrySignature)) &&
      MatchBytes(image + kSkillLookupRva, kSkillLookupSignature,
                  sizeof(kSkillLookupSignature)) &&
      MatchBytes(image + kMenuInventoryPlayerLoadRva,
                 kMenuInventoryPlayerLoadSignature,
                 sizeof(kMenuInventoryPlayerLoadSignature)) &&
      MatchBytes(image + kDefaultProgressionOwnerLoadRva,
                 kDefaultProgressionOwnerLoadSignature,
                 sizeof(kDefaultProgressionOwnerLoadSignature)) &&
       MatchBytes(image + kVariableOwnerSelectRva,
                  kVariableOwnerSelectSignature,
                  sizeof(kVariableOwnerSelectSignature)) &&
       MatchBytes(image + kCharacterEnumLoadRva,
                  kCharacterEnumLoadSignature,
                  sizeof(kCharacterEnumLoadSignature));
  cached = valid ? 1 : 0;
  cached_image = valid ? image : nullptr;
  if (valid) {
    DIS_INFO("[gameplay] verified direct PlayerDI/LogicalPlayer bridge");
  } else {
    DIS_WARN("[gameplay] direct bridge disabled: gamedll identity/signature mismatch");
  }
  return {cached_image, valid};
}

uint8_t* CompletePlayerObject(const CRTTIObject* reflected) {
  if (!reflected) return nullptr;
  void* vtable_raw = nullptr;
  if (!Read(reflected, &vtable_raw) || !vtable_raw) return nullptr;
  auto** vtable = static_cast<void**>(vtable_raw);
  void* locator_raw = nullptr;
  if (!Read(vtable - 1, &locator_raw) || !locator_raw) return nullptr;
  CompleteObjectLocator64 locator = {};
  if (!Read(locator_raw, &locator) || locator.signature != 1 ||
      locator.subobject_offset > 4096) {
    return nullptr;
  }
  const uintptr_t address = reinterpret_cast<uintptr_t>(reflected);
  if (locator.subobject_offset > address) return nullptr;
  auto* complete =
      reinterpret_cast<uint8_t*>(address - locator.subobject_offset);
  return Readable(complete, kExtraCapacityOffset + sizeof(uint32_t))
             ? complete
             : nullptr;
}

uint8_t* MenuPlayerObject(const Build& build, void** root_out) {
  if (root_out) *root_out = nullptr;
  if (!build.valid || !build.image) return nullptr;
  void* gameplay_root = nullptr;
  void* player = nullptr;
  if (!Read(build.image + kGameplayRootRva, &gameplay_root) ||
      !gameplay_root ||
      !Read(static_cast<uint8_t*>(gameplay_root) +
                kGameplayRootPlayerOffset,
            &player) ||
      !player ||
      !Readable(player, kExtraCapacityOffset + sizeof(uint32_t))) {
    return nullptr;
  }
  if (root_out) *root_out = gameplay_root;
  auto* complete = static_cast<uint8_t*>(player);
  if (g_state.player != complete) {
    CRTTIObject* reflected = TheRtti().AsRttiObject(complete);
    if (!reflected || TheRtti().ClassName(reflected) != "PlayerDI" ||
        CompletePlayerObject(reflected) != complete) {
      DIS_WARN("[gameplay] rejected gameplay-root player %p: not PlayerDI",
               complete);
      return nullptr;
    }
  }
  return complete;
}

uint8_t* ActiveProgressionOwner(const Build& build, uint8_t* player,
                                bool* uses_default) {
  if (uses_default) *uses_default = false;
  if (!build.valid || !build.image || !player) return nullptr;

  uint8_t default_source = 0;
  void* owner = nullptr;
  if (!Read(player + kVariableSourceFlagOffset, &default_source)) {
    return nullptr;
  }
  if (default_source) {
    if (!Read(build.image + kDefaultProgressionOwnerRva, &owner)) {
      return nullptr;
    }
    if (uses_default) *uses_default = true;
  } else if (!Read(player + kLogicalPlayerOffset, &owner)) {
    return nullptr;
  }

  // SkillRank, CanBuy and Purchase all consume this LogicalPlayer-layout
  // owner and access at least the learned-skill tree at +0xB8.
  return owner && Readable(owner, 0xE0) ? static_cast<uint8_t*>(owner)
                                        : nullptr;
}

void ResetForPlayer(void* gameplay_root, uint8_t* complete, uint8_t* logical,
                    bool uses_default_progression) {
  if (g_state.gameplay_root == gameplay_root &&
      g_state.player == complete && g_state.logical == logical &&
      g_state.uses_default_progression == uses_default_progression) {
    return;
  }
  g_state = BridgeState{};
  g_state.gameplay_root = gameplay_root;
  g_state.player = complete;
  g_state.logical = logical;
  g_state.uses_default_progression = uses_default_progression;
  if (complete) {
    uint8_t* scene_player = CompletePlayerObject(TheGame().player());
    DIS_INFO("[gameplay] root=%p menu PlayerDI=%p ProgressionOwner=%p (%s, sourceFlag=%u)",
             gameplay_root, complete, logical,
             uses_default_progression ? "global" : "per-save",
             uses_default_progression ? 1u : 0u);
    if (scene_player != complete) {
      DIS_INFO("[gameplay] scene PlayerDI=%p is not the menu-owned local player",
               scene_player);
    }
  }
}

bool Context(PlayerContext* out) {
  if (!out) return false;
  *out = {};
  const Build build = VerifiedBuild();
  if (!build.valid) return false;

  // This is the same path MenuInventory executes. The reflected PlayerDI
  // returned by the level's actor scan is a scene-side actor and can have a
  // null +0xA18 and empty containers even while the local gameplay player is
  // fully initialized.
  void* gameplay_root = nullptr;
  uint8_t* complete = MenuPlayerObject(build, &gameplay_root);
  if (!complete) {
    ResetForPlayer(nullptr, nullptr, nullptr, false);
    return false;
  }

  bool uses_default_progression = false;
  uint8_t* logical =
      ActiveProgressionOwner(build, complete, &uses_default_progression);
  ResetForPlayer(gameplay_root, complete, logical,
                 uses_default_progression);
  out->complete = complete;
  out->logical = logical;
  out->uses_default_progression = uses_default_progression;
  // Inventory, quick slots and held items belong directly to PlayerDI and do
  // not need the progression owner. Skills independently report unavailable
  // until that owner exists.
  return true;
}

std::string ReadTtl(const TtlString* source) {
  TtlString value = {};
  if (!source || !Read(source, &value) || value.size > 4096 ||
      value.capacity < value.size) {
    return {};
  }
  if (value.size == 0) return {};
  if (!value.data || !Readable(value.data, value.size)) return {};
  return std::string(value.data, value.size);
}

std::string ReadTtlWide(const TtlWideString* source) {
  TtlWideString value = {};
  if (!source || !Read(source, &value) || value.size > 4096 ||
      value.capacity < value.size) {
    return {};
  }
  if (value.size == 0) return {};
  const size_t bytes = static_cast<size_t>(value.size) * sizeof(wchar_t);
  if (!value.data || !Readable(value.data, bytes)) return {};

  int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data,
                                  static_cast<int>(value.size), nullptr, 0,
                                  nullptr, nullptr);
  DWORD flags = WC_ERR_INVALID_CHARS;
  if (count <= 0) {
    // A malformed legacy localization entry should not blank the whole row.
    // Retry permissively, matching Windows' normal replacement behavior.
    flags = 0;
    count = WideCharToMultiByte(CP_UTF8, flags, value.data,
                                static_cast<int>(value.size), nullptr, 0,
                                nullptr, nullptr);
  }
  if (count <= 0) return {};
  std::string result(static_cast<size_t>(count), '\0');
  if (WideCharToMultiByte(CP_UTF8, flags, value.data,
                          static_cast<int>(value.size), result.data(), count,
                          nullptr, nullptr) != count) {
    return {};
  }
  return result;
}

std::string Humanize(const std::string& source) {
  std::string out;
  out.reserve(source.size() + 12);
  bool previous_lower = false;
  for (unsigned char c : source) {
    if (c == '$') continue;
    if (c == '_' || c == '-' || c == '.' || c == '/' || c == '\\') {
      if (!out.empty() && out.back() != ' ') out.push_back(' ');
      previous_lower = false;
      continue;
    }
    const bool upper = std::isupper(c) != 0;
    if (upper && previous_lower && !out.empty() && out.back() != ' ')
      out.push_back(' ');
    out.push_back(static_cast<char>(c));
    previous_lower = std::islower(c) != 0 || std::isdigit(c) != 0;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  size_t first = 0;
  while (first < out.size() && out[first] == ' ') ++first;
  if (first) out.erase(0, first);
  return out;
}

std::string TranslateKey(const std::string& key) {
  if (key.empty()) return {};
  const Build build = VerifiedBuild();
  if (!build.valid) return {};
  void* manager = nullptr;
  void* vtable_raw = nullptr;
  void* translate_raw = nullptr;
  if (!Read(build.image + kLocalizationManagerRva, &manager) || !manager ||
      !Read(manager, &vtable_raw) || !vtable_raw ||
      !Read(static_cast<uint8_t*>(vtable_raw) + 0x178, &translate_raw) ||
      !translate_raw || !Executable(translate_raw, 1)) {
    return {};
  }

  TtlString input = {key.data(), static_cast<uint32_t>(key.size()),
                     static_cast<uint32_t>(key.size())};
  TtlWideString output = {};
  using TranslateFn =
      TtlWideString* (*)(void*, TtlWideString*, const TtlString*, bool);
  reinterpret_cast<TranslateFn>(translate_raw)(manager, &output, &input, true);
  const std::string translated = ReadTtlWide(&output);

  // The native callers release this exact output through MSVCR100!free.
  if (output.data) {
    HMODULE crt = GetModuleHandleW(L"MSVCR100.dll");
    FARPROC free_proc = crt ? GetProcAddress(crt, "free") : nullptr;
    if (free_proc) {
      using FreeFn = void (*)(const void*);
      reinterpret_cast<FreeFn>(free_proc)(output.data);
    }
  }
  return translated;
}

bool GameVtable(void* object, size_t slot, void** target) {
  if (target) *target = nullptr;
  const Build build = VerifiedBuild();
  void* vtable = nullptr;
  void* function = nullptr;
  if (!build.valid || !object || !target || !Read(object, &vtable) ||
      !vtable ||
      !Read(static_cast<uint8_t*>(vtable) + slot, &function) || !function ||
      !Executable(function, 1)) {
    return false;
  }
  const uintptr_t address = reinterpret_cast<uintptr_t>(function);
  const uintptr_t base = reinterpret_cast<uintptr_t>(build.image);
  if (address < base || address >= base + kGameImageSize) return false;
  *target = function;
  return true;
}

bool ItemValid(void* item) {
  if (!item || !Readable(item, kItemDescriptorOffset + sizeof(void*)))
    return false;
  void* item_vtable = nullptr;
  void* name_function = nullptr;
  const Build build = VerifiedBuild();
  if (!build.valid || !Read(item, &item_vtable) || !item_vtable ||
      !GameVtable(static_cast<uint8_t*>(item) + kItemDescriptorOffset,
                  0xA0, &name_function)) {
    return false;
  }
  const uintptr_t vtable = reinterpret_cast<uintptr_t>(item_vtable);
  const uintptr_t base = reinterpret_cast<uintptr_t>(build.image);
  return vtable >= base && vtable < base + kGameImageSize &&
         name_function == build.image + kItemNameRva;
}

int ItemVirtualInt(void* item, size_t slot, int minimum, int maximum) {
  void* function = nullptr;
  void* descriptor =
      item ? static_cast<uint8_t*>(item) + kItemDescriptorOffset : nullptr;
  if (!GameVtable(descriptor, slot, &function)) return -1;
  using Fn = int (*)(void*);
  const int value = reinterpret_cast<Fn>(function)(descriptor);
  return value >= minimum && value <= maximum ? value : -1;
}

uint64_t ItemDescriptorIdentity(void* item) {
  if (!item) return 0;
  const uint8_t* descriptor =
      static_cast<const uint8_t*>(item) + kItemDescriptorOffset;
  uintptr_t base_descriptor = 0;
  uintptr_t modifier = 0;
  uint64_t flags = 0;
  uint64_t cached_ids = 0;
  Read(descriptor + 0x08, &base_descriptor);
  Read(descriptor + 0x10, &flags);
  Read(descriptor + 0x18, &modifier);
  Read(descriptor + 0x20, &cached_ids);
  return static_cast<uint64_t>(base_descriptor) ^
         (static_cast<uint64_t>(modifier) << 7) ^
         (flags * 0x9E3779B185EBCA87ull) ^
         (cached_ids * 0xC2B2AE3D27D4EB4Full);
}

const ItemMeta* Metadata(void* item) {
  if (!ItemValid(item)) return nullptr;
  const uint64_t identity = ItemDescriptorIdentity(item);
  const auto found = g_state.item_cache.find(item);
  if (found != g_state.item_cache.end() &&
      found->second.descriptor_identity == identity) {
    return &found->second;
  }
  if (found != g_state.item_cache.end()) g_state.item_cache.erase(found);

  ItemMeta meta;
  meta.descriptor_identity = identity;
  const Build build = VerifiedBuild();
  using NameFn = const TtlString* (*)(void*);
  const TtlString* key = reinterpret_cast<NameFn>(
      build.image + kItemNameRva)(
          static_cast<uint8_t*>(item) + kItemDescriptorOffset);
  meta.key = ReadTtl(key);
  meta.name = TranslateKey(meta.key);
  if (meta.name.empty()) meta.name = Humanize(meta.key);
  if (meta.name.empty()) meta.name = "Inventory Item";
  meta.category = ItemVirtualInt(item, 0x88, -1, 11);
  meta.type = ItemVirtualInt(item, 0x90, -1, 64);
  meta.required_level = ItemVirtualInt(item, 0x38, 0, 100);
  return &g_state.item_cache.emplace(item, std::move(meta)).first->second;
}

const char* CategoryName(int category) {
  switch (category) {
    case 0: return "MELEE";
    case 1: return "FIREARM";
    case 2: return "THROWABLE";
    case 3: return "CONSUMABLE";
    case 4: return "AMMUNITION";
    case 5: return "CASH";
    case 6: return "CHAINSAW";
    case 7: return "COLLECTABLE";
    case 8: return "MEDKIT";
    case 9: return "CRAFT PART";
    case 10: return "ITEM";
    case 11: return "QUEST ITEM";
    default: return "ITEM";
  }
}

uint32_t ItemCount(void* item) {
  uint32_t count = 1;
  if (!item || !Read(static_cast<uint8_t*>(item) + kItemCountOffset, &count) ||
      count == 0 || count > 1000000) {
    return 1;
  }
  return count;
}

std::string PointerId(void* item) {
  char text[32] = {};
  std::snprintf(text, sizeof(text), "item:%p", item);
  return text;
}

NativeUiRow ItemRow(void* item, int ordinal, bool selected) {
  NativeUiRow row;
  row.native_ordinal = ordinal;
  row.selected = selected;
  const ItemMeta* meta = Metadata(item);
  if (!meta) return row;
  row.id = PointerId(item);
  row.icon_texture = meta->key;
  row.item_category = meta->category;
  row.enabled = true;
  row.text.push_back(meta->name);
  std::string detail = CategoryName(meta->category);
  const uint32_t count = ItemCount(item);
  if (count > 1) detail += "  x" + std::to_string(count);
  row.text.push_back(std::move(detail));
  if (meta->required_level > 0) {
    row.text.push_back("REQUIRED LEVEL " +
                       std::to_string(meta->required_level));
  }
  return row;
}

bool ReadPointerArray(uint8_t* player, size_t array_offset,
                      size_t count_offset, size_t capacity_offset,
                      uint32_t hard_limit, std::vector<void*>* out,
                      bool retain_nulls) {
  if (!player || !out) return false;
  void* array = nullptr;
  uint32_t count = 0;
  uint32_t capacity = 0;
  if (!Read(player + array_offset, &array) ||
      !Read(player + count_offset, &count) ||
      !Read(player + capacity_offset, &capacity) ||
      count > capacity || capacity > hard_limit ||
      (count && (!array ||
                 !Readable(array, static_cast<size_t>(count) *
                                      sizeof(void*))))) {
    return false;
  }
  out->clear();
  out->reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    void* item = nullptr;
    if (!Read(static_cast<uint8_t*>(array) + i * sizeof(void*), &item))
      return false;
    if (retain_nulls || item) out->push_back(item);
  }
  return true;
}

void LogPointerArrayHeader(uint8_t* player, const char* name,
                           size_t array_offset, size_t count_offset,
                           size_t capacity_offset) {
  void* data = nullptr;
  uint32_t count = 0;
  uint32_t capacity = 0;
  const bool data_ok = Read(player + array_offset, &data);
  const bool count_ok = Read(player + count_offset, &count);
  const bool capacity_ok = Read(player + capacity_offset, &capacity);
  DIS_INFO("[gameplay] %-9s data=%p count=%u capacity=%u readable=%u",
           name, data, count, capacity,
           data_ok && count_ok && capacity_ok && count <= capacity &&
                   (!count || (data && Readable(
                       data, static_cast<size_t>(count) * sizeof(void*))))
               ? 1u
               : 0u);
}

bool CollectInventory(const PlayerContext& context,
                       std::vector<void*>* items,
                       std::vector<void*>* quick) {
  if (!items || !quick) return false;
  if (!g_state.containers_logged) {
    g_state.containers_logged = true;
    LogPointerArrayHeader(context.complete, "inventory",
                          kInventoryArrayOffset, kInventoryCountOffset,
                          kInventoryCapacityOffset);
    LogPointerArrayHeader(context.complete, "extra", kExtraArrayOffset,
                          kExtraCountOffset, kExtraCapacityOffset);
    LogPointerArrayHeader(context.complete, "quick", kQuickArrayOffset,
                          kQuickCountOffset, kQuickCapacityOffset);
  }
  std::vector<void*> main_items;
  std::vector<void*> extra_items;
  if (!ReadPointerArray(context.complete, kInventoryArrayOffset,
                        kInventoryCountOffset, kInventoryCapacityOffset,
                        512, &main_items, false) ||
      !ReadPointerArray(context.complete, kExtraArrayOffset,
                        kExtraCountOffset, kExtraCapacityOffset,
                        512, &extra_items, false) ||
      !ReadPointerArray(context.complete, kQuickArrayOffset,
                        kQuickCountOffset, kQuickCapacityOffset,
                        32, quick, true)) {
    return false;
  }

  items->clear();
  items->reserve(main_items.size() + extra_items.size());
  std::unordered_set<void*> seen;
  for (void* item : main_items) {
    if (ItemValid(item) && seen.insert(item).second) items->push_back(item);
  }
  for (void* item : extra_items) {
    if (ItemValid(item) && seen.insert(item).second) items->push_back(item);
  }
  for (void*& item : *quick) {
    if (item && !ItemValid(item)) item = nullptr;
  }
  return true;
}

void* EquippedItem(uint8_t* player) {
  if (!player) return nullptr;
  void* handle = nullptr;
  void* item = nullptr;
  if (Read(player + kPrimaryHandHandleOffset, &handle) && handle &&
      Read(player + kPrimaryHandItemOffset, &item) && ItemValid(item)) {
    return item;
  }
  handle = nullptr;
  item = nullptr;
  if (Read(player + kSecondaryHandHandleOffset, &handle) && handle &&
      Read(player + kSecondaryHandItemOffset, &item) && ItemValid(item)) {
    return item;
  }
  return nullptr;
}

bool ItemStillOwned(void* item, std::vector<void*>* inventory = nullptr) {
  PlayerContext context;
  std::vector<void*> items;
  std::vector<void*> quick;
  if (!item || !Context(&context) ||
      !CollectInventory(context, &items, &quick)) {
    return false;
  }
  if (inventory) *inventory = items;
  // Quick-slot entries are authoritative owned item references too, but some
  // saves do not repeat those pointers in the two inventory arrays. Requiring
  // pointer identity with only `items` rejected a valid quick-slot tap before
  // the verified vanilla equip call could run.
  return std::find(items.begin(), items.end(), item) != items.end() ||
         std::find(quick.begin(), quick.end(), item) != quick.end();
}

bool UseOrEquip(void* item) {
  PlayerContext context;
  std::vector<void*> items;
  std::vector<void*> quick;
  if (!item || !Context(&context) ||
      !CollectInventory(context, &items, &quick)) {
    DIS_WARN("[gameplay] equip/use rejected: player or containers unavailable");
    return false;
  }
  const bool owned =
      std::find(items.begin(), items.end(), item) != items.end() ||
      std::find(quick.begin(), quick.end(), item) != quick.end();
  if (!owned) {
    DIS_WARN("[gameplay] equip/use rejected: item %p is no longer owned", item);
    return false;
  }
  const Build build = VerifiedBuild();
  using EquipFn = void (*)(void*, int, void*);
  reinterpret_cast<EquipFn>(build.image + kEquipOrUseRva)(
      context.complete, 0, item);
  DIS_INFO("[gameplay] equip/use item %p", item);
  return true;
}

std::string CharacterFromPlayer(uint8_t* player) {
  int32_t character = -1;
  if (!player || !Read(player + kCharacterEnumOffset, &character)) return {};
  // This is the exact switch table used by the game's character-name helper:
  // 0 Xian, 1 Purna, 2 Sam B, 3 Logan, 5 Ryder. Value 4 is not a playable
  // Dead Island character and must not be silently assigned a skill catalog.
  switch (character) {
    case 0: return "xian";
    case 1: return "purna";
    case 2: return "samb";
    case 3: return "logan";
    case 5: return "ryder";
    default: return {};
  }
}

TtlString View(const std::string& text) {
  return {text.data(), static_cast<uint32_t>(text.size()),
          static_cast<uint32_t>(text.size())};
}

int SkillRank(uint8_t* logical, const std::string& id) {
  if (!logical || id.empty()) return 0;
  const Build build = VerifiedBuild();
  TtlString view = View(id);
  using Fn = int (*)(void*, const TtlString*);
  const int rank = reinterpret_cast<Fn>(build.image + kSkillRankRva)(
      logical, &view);
  return rank >= 0 && rank <= 64 ? rank : 0;
}

int SkillInternalId(const std::string& id) {
  if (id.empty()) return -1;
  const Build build = VerifiedBuild();
  using RegistryFn = void* (*)();
  using LookupFn = void* (*)(void*, const TtlString*);
  void* registry =
      reinterpret_cast<RegistryFn>(build.image + kSkillRegistryRva)();
  if (!registry || !Readable(static_cast<uint8_t*>(registry) + 0x18,
                             sizeof(void*))) {
    return -1;
  }
  TtlString view = View(id);
  void* node = reinterpret_cast<LookupFn>(build.image + kSkillLookupRva)(
      static_cast<uint8_t*>(registry) + 0x18, &view);
  uint16_t internal = 0;
  if (!node ||
      !Read(static_cast<uint8_t*>(node) - sizeof(uint64_t), &internal)) {
    return -1;
  }
  return internal == 0xFFFF ? -1 : static_cast<int>(internal);
}

int SkillAvailability(uint8_t* logical, const std::string& id) {
  const int internal = SkillInternalId(id);
  if (!logical || internal < 0 || internal > 0xFFFE) return 4;
  const Build build = VerifiedBuild();
  using Fn = int (*)(void*, uint16_t, bool);
  return reinterpret_cast<Fn>(build.image + kSkillCanBuyRva)(
      logical, static_cast<uint16_t>(internal), true);
}

std::string DetectCharacter(uint8_t* player, uint8_t* logical) {
  // PlayerDI+0x1B5C is what the shipped menu passes to the game's character
  // name switch. Unlike a localized postfix or a learned root rank, it is
  // already populated for a brand-new level-1 save.
  std::string character = CharacterFromPlayer(player);
  if (!character.empty()) return character;

  // Safe fallback for imported/modded saves whose postfix string differs:
  // score only the authored root skills. At least one root is initialized by
  // the game for a playable character, and this runs once per player object.
  static const char* characters[] = {"samb", "xian", "logan", "purna",
                                     "ryder"};
  int best_score = 0;
  for (const char* candidate : characters) {
    int score = 0;
    for (std::size_t i = 0; i < kDeadIslandSkillCount; ++i) {
      const SkillCatalogEntry& skill = kDeadIslandSkills[i];
      if (skill.tier == 0 &&
          std::strcmp(skill.character, candidate) == 0) {
        score += SkillRank(logical, skill.id);
      }
    }
    if (score > best_score) {
      best_score = score;
      character = candidate;
    }
  }
  return character;
}

const SkillCatalogEntry* FindSkill(const std::string& id) {
  for (std::size_t i = 0; i < kDeadIslandSkillCount; ++i) {
    if (id == kDeadIslandSkills[i].id) return &kDeadIslandSkills[i];
  }
  return nullptr;
}

}  // namespace

bool Supported() { return VerifiedBuild().valid; }

void Reset() { g_state = BridgeState{}; }

bool SampleInventory(InventorySnapshot* inventory, GearSnapshot* gear,
                     HudSnapshot* hud) {
  if (!inventory || !gear || !Supported()) return false;
  *inventory = {};
  *gear = {};

  PlayerContext context;
  if (!Context(&context)) {
    inventory->unavailable_reason = "Waiting for the live player inventory.";
    gear->unavailable_reason = "Waiting for the live player quick slots.";
    return true;
  }

  std::vector<void*> items;
  std::vector<void*> quick;
  if (!CollectInventory(context, &items, &quick)) {
    inventory->unavailable_reason =
        "The live inventory container is not ready yet.";
    gear->unavailable_reason =
        "The live quick-slot container is not ready yet.";
    return true;
  }
  g_state.inventory_items = items;
  g_state.quick_items = quick;

  if (g_state.selected_item &&
      std::find(items.begin(), items.end(), g_state.selected_item) ==
          items.end()) {
    g_state.selected_item = nullptr;
  }
  void* equipped = EquippedItem(context.complete);
  if (!g_state.selected_item) {
    g_state.selected_item = equipped ? equipped
                                     : (items.empty() ? nullptr : items.front());
  }

  inventory->available = true;
  inventory->items.reserve(items.size());
  for (std::size_t i = 0; i < items.size(); ++i) {
    NativeUiRow row =
        ItemRow(items[i], static_cast<int>(i),
                items[i] == g_state.selected_item);
    // Preserve the exact pointer-array ordinal even if a damaged/modded item
    // cannot expose metadata. UI actions address g_state.inventory_items by
    // this snapshot index; compacting failed rows would silently redirect
    // every later Equip/Drop/quick-slot action to the wrong object.
    if (row.id.empty()) {
      row.id = "unavailable:" + std::to_string(i);
      row.native_ordinal = static_cast<int>(i);
      row.enabled = false;
      row.text = {"UNAVAILABLE ITEM", "METADATA COULD NOT BE READ"};
    }
    inventory->items.push_back(std::move(row));
  }
  if (const ItemMeta* selected = Metadata(g_state.selected_item)) {
    inventory->selected_name = selected->name;
    inventory->description = CategoryName(selected->category);
    inventory->equip_control_enabled = true;
  }

  gear->available = true;
  const std::size_t live_quick_slots = std::min<std::size_t>(quick.size(), 9);
  gear->quick_slots.reserve(live_quick_slots);
  for (std::size_t slot = 0; slot < live_quick_slots; ++slot) {
    NativeUiRow row;
    row.native_ordinal = static_cast<int>(slot);
    row.id = "quick:" + std::to_string(slot);
    void* item = slot < quick.size() ? quick[slot] : nullptr;
    if (item) {
      row = ItemRow(item, static_cast<int>(slot), item == equipped);
      row.id = "quick:" + std::to_string(slot);
    }
    gear->quick_slots.push_back(std::move(row));
  }

  if (hud && equipped) {
    if (const ItemMeta* meta = Metadata(equipped)) {
      hud->available = true;
      hud->equipped_name = meta->name;
      hud->icon_texture = meta->key;
      const uint32_t count = ItemCount(equipped);
      if (count > 1) hud->held_quantity = std::to_string(count);
    }
  }

  if (!g_state.inventory_logged) {
    g_state.inventory_logged = true;
    DIS_INFO("[gameplay] direct inventory=%d quick-slots=%d equipped=%p",
             static_cast<int>(inventory->items.size()),
             static_cast<int>(quick.size()), equipped);
  }
  return true;
}

bool SampleSkills(const Vitals& vitals, SkillsSnapshot* skills) {
  if (!skills || !Supported()) return false;
  *skills = {};
  PlayerContext context;
  if (!Context(&context) || !context.logical) {
    skills->unavailable_reason = "Waiting for the live skill owner.";
    return true;
  }
  if (g_state.character.empty())
    g_state.character = DetectCharacter(context.complete, context.logical);
  if (g_state.character.empty()) {
    skills->unavailable_reason =
        "The current character's skill definition was not identified.";
    return true;
  }

  struct LiveSkill {
    const SkillCatalogEntry* authored = nullptr;
    int rank = 0;
    int availability = 4;
  };
  std::vector<LiveSkill> live;
  for (std::size_t i = 0; i < kDeadIslandSkillCount; ++i) {
    const SkillCatalogEntry& authored = kDeadIslandSkills[i];
    if (g_state.character != authored.character) continue;
    LiveSkill value;
    value.authored = &authored;
    value.rank = SkillRank(context.logical, authored.id);
    value.availability =
        value.rank < authored.max_rank
            ? SkillAvailability(context.logical, authored.id)
            : 1;
    live.push_back(value);
  }
  if (live.empty()) {
    skills->unavailable_reason = "No authored skills match this character.";
    return true;
  }

  bool selection_exists = false;
  for (const LiveSkill& value : live) {
    if (g_state.selected_skill == value.authored->id) {
      selection_exists = true;
      break;
    }
  }
  if (!selection_exists) {
    g_state.selected_skill.clear();
    for (const LiveSkill& value : live) {
      if (value.rank > 0) {
        g_state.selected_skill = value.authored->id;
        break;
      }
    }
    if (g_state.selected_skill.empty())
      g_state.selected_skill = live.front().authored->id;
  }

  skills->available = true;
  skills->available_points =
      vitals.skill_points.valid ? std::to_string(vitals.skill_points.i())
                                : std::string();
  g_state.skill_ids.clear();
  g_state.skill_ids.reserve(live.size());
  for (std::size_t i = 0; i < live.size(); ++i) {
    const LiveSkill& value = live[i];
    const SkillCatalogEntry& authored = *value.authored;
    NativeUiRow row;
    row.id = authored.id;
    row.icon_texture = authored.id;
    row.group = authored.tree;
    row.tier = authored.tier;
    row.native_ordinal = static_cast<int>(i);
    row.native_x = authored.branch_count <= 1
        ? 0.0f
        : static_cast<float>(authored.branch) * 100.0f;
    row.native_y = static_cast<float>(authored.tier) * 100.0f;
    row.has_native_position = true;
    // Every node remains selectable so locked branches can still show their
    // real rank and requirement. Purchasability is a separate state.
    row.enabled = true;
    row.purchasable = context.uses_default_progression &&
                      value.availability == 0 &&
                      value.rank < authored.max_rank;
    row.selected = g_state.selected_skill == authored.id;
    row.text.push_back(Humanize(authored.id));
    row.text.push_back(std::to_string(value.rank) + "/" +
                       std::to_string(authored.max_rank));
    if (value.rank >= authored.max_rank) {
      row.text.push_back("MAXIMUM RANK");
    } else if (row.purchasable) {
      row.text.push_back("AVAILABLE");
    } else if (value.rank > 0) {
      row.text.push_back("OWNED / NEXT RANK LOCKED");
    } else {
      row.text.push_back("LOCKED");
    }
    skills->skills.push_back(std::move(row));
    g_state.skill_ids.push_back(authored.id);

    if (g_state.selected_skill == authored.id) {
      skills->selected_name = Humanize(authored.id);
      skills->selected_level =
          "RANK " + std::to_string(value.rank) + " / " +
          std::to_string(authored.max_rank);
      if (authored.rank_values && *authored.rank_values) {
        skills->description =
            std::string("RANK EFFECTS  ") + authored.rank_values;
      } else if (authored.requirement_points > 0) {
        skills->description =
            "Requires " + std::to_string(authored.requirement_points) +
            " point" + (authored.requirement_points == 1 ? "" : "s") +
            " in the previous tier.";
      } else {
        skills->description =
            "This is the opening skill for its native Dead Island tree.";
      }
      skills->learn_control_enabled =
          context.uses_default_progression && value.availability == 0 &&
          value.rank < authored.max_rank;
    }
  }

  if (!g_state.skills_logged) {
    g_state.skills_logged = true;
    DIS_INFO("[gameplay] direct skills character='%s' nodes=%d",
             g_state.character.c_str(),
             static_cast<int>(skills->skills.size()));
  }
  return true;
}

bool SelectInventoryItem(std::size_t index) {
  if (!Supported() || index >= g_state.inventory_items.size()) return false;
  void* item = g_state.inventory_items[index];
  if (!ItemStillOwned(item)) return false;
  g_state.selected_item = item;
  return true;
}

bool AssignInventoryItemToQuickSlot(std::size_t inventory_index,
                                    std::size_t quick_slot) {
  if (!Supported() || inventory_index >= g_state.inventory_items.size()) {
    return false;
  }

  // Never trust the pointer/index copied into the rendered snapshot by
  // itself. Reacquire PlayerDI and both owned containers on the game thread,
  // then require exact membership before entering the native mutator.
  void* wanted = g_state.inventory_items[inventory_index];
  PlayerContext context;
  std::vector<void*> items;
  std::vector<void*> quick;
  if (!wanted || !Context(&context) ||
      !CollectInventory(context, &items, &quick) ||
      quick_slot >= quick.size() || quick_slot >= 9 ||
      std::find(items.begin(), items.end(), wanted) == items.end()) {
    DIS_WARN("[gameplay] quick-slot assignment rejected item=%p index=%u "
             "slot=%u owned=%u quick-count=%u",
             wanted, static_cast<unsigned>(inventory_index),
             static_cast<unsigned>(quick_slot),
             static_cast<unsigned>(std::find(items.begin(), items.end(),
                                             wanted) != items.end()),
             static_cast<unsigned>(quick.size()));
    return false;
  }

  const Build build = VerifiedBuild();
  using AssignFn = int (*)(void*, void*, uint32_t);
  const int result = reinterpret_cast<AssignFn>(
      build.image + kAssignQuickSlotRva)(
          context.complete, wanted, static_cast<uint32_t>(quick_slot));
  if (result != 0) {
    DIS_WARN("[gameplay] quick-slot assignment item=%p slot=%u rejected "
             "by game (code=%d)", wanted,
             static_cast<unsigned>(quick_slot), result);
    return false;
  }

  // A successful native assignment removes the item from its old container,
  // and may displace another item. Discard all copied row identities so the
  // next sample is authoritative before another touch action can run.
  g_state.selected_item = nullptr;
  g_state.inventory_items.clear();
  g_state.quick_items.clear();
  DIS_INFO("[gameplay] assigned item %p to quick slot %u", wanted,
           static_cast<unsigned>(quick_slot));
  return true;
}

bool ActivateQuickSlot(std::size_t index) {
  if (!Supported()) return false;
  PlayerContext context;
  std::vector<void*> items;
  std::vector<void*> quick;
  if (!Context(&context) || !CollectInventory(context, &items, &quick) ||
      index >= quick.size() || !quick[index]) {
    DIS_WARN("[gameplay] quick-slot %u rejected (count=%u item=%p)",
             static_cast<unsigned>(index),
             static_cast<unsigned>(quick.size()),
             index < quick.size() ? quick[index] : nullptr);
    return false;
  }
  DIS_INFO("[gameplay] quick-slot %u activate item=%p",
           static_cast<unsigned>(index), quick[index]);
  return UseOrEquip(quick[index]);
}

bool SelectGearSlot(std::size_t index) { return ActivateQuickSlot(index); }

bool InventoryAction(InventoryNativeAction action) {
  if (!Supported()) return false;
  if (action != InventoryNativeAction::kEquip || !g_state.selected_item)
    return false;
  return UseOrEquip(g_state.selected_item);
}

bool SelectSkill(std::size_t index) {
  if (!Supported() || index >= g_state.skill_ids.size()) return false;
  g_state.selected_skill = g_state.skill_ids[index];
  return true;
}

bool SkillAction(SkillNativeAction action) {
  if (!Supported() || action != SkillNativeAction::kLearn ||
      g_state.selected_skill.empty()) {
    return false;
  }
  PlayerContext context;
  if (!Context(&context) || !context.logical) return false;
  // The verified vanilla caller invokes PurchaseByName directly only for the
  // global/local progression branch. A per-save owner can represent a remote
  // or replicated player and requires a different network mutation path.
  if (!context.uses_default_progression) {
    DIS_WARN("[gameplay] direct skill purchase refused for per-save owner");
    return false;
  }
  const SkillCatalogEntry* authored = FindSkill(g_state.selected_skill);
  if (!authored ||
      SkillRank(context.logical, g_state.selected_skill) >=
          authored->max_rank ||
      SkillAvailability(context.logical, g_state.selected_skill) != 0) {
    return false;
  }

  TtlString view = View(g_state.selected_skill);
  const Build build = VerifiedBuild();
  using PurchaseFn = int (*)(void*, const TtlString*, int);
  const int result =
      reinterpret_cast<PurchaseFn>(
          build.image + kSkillPurchaseByNameRva)(context.logical, &view, 0);
  DIS_INFO("[gameplay] purchase skill '%s' result=%d",
           g_state.selected_skill.c_str(), result);
  return result == 0;
}

}  // namespace gameplay
}  // namespace dis

#include "InstalledAssetProvider.h"

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <list>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fomds {
namespace {

constexpr char kOriginalArchiveSha256[] =
    "e55ef6bcaa7430008a6fcae7980ef06ca6912bb10431c177807b366a0fc5a450";
constexpr std::uint64_t kOriginalArchiveSize = 625619136ULL;
constexpr std::uint64_t kOriginalArchiveEntries = 122403ULL;
constexpr char kPatchedArchiveSha256[] =
    "8faf44883c3756d317bed2ddaf1881c79c85402a67374e7dcccbc54998667b59";
constexpr std::uint64_t kPatchedArchiveSize = 625762141ULL;
constexpr std::uint64_t kPatchedArchiveEntries = 122404ULL;
constexpr char kPatchManifestSha256[] =
    "d86ea8a05427a3512fa785b0562c1df05f2a5df63310435546494cd1f264361f";
constexpr char kPatchedGameSha256[] =
    "9a14923fafb91a3a79cf2e0724445baaa958ee632c75c1fc7f9d51d184997293";
constexpr char kBridgeSha256[] =
    "1f0d300410e9f5d1358e9133a2cf0dcab56920f7adf1e989778c73bd638830b2";
constexpr char kExpectedExecutableSha256[] =
    "aed5b1f5d5cd75afee86f0cf347655a7e62be361d775d6ffc0671b388660ee2e";
constexpr char kAssetOwner[] = "FieldsOfMistriaSecondScreen:assets:v1";
constexpr wchar_t kLiveArchiveName[] = L"assets.zip";
constexpr wchar_t kBackupArchiveName[] =
    L"assets.zip.fomds-original-e55ef6bcaa743000";
constexpr wchar_t kOwnerName[] =
    L"FieldsOfMistriaSecondScreen.assets.owner";
constexpr wchar_t kDeploymentManifestName[] =
    L"FieldsOfMistriaSecondScreen.assets.manifest.json";

struct AssetSpec {
  InstalledAssetKey key;
  InstalledAssetKind kind;
  std::string data_path;
  std::string metadata_path;
  std::string asset_id;
  std::uint32_t frame_width;
  std::uint32_t frame_height;
  std::uint32_t frame_count;
  std::string atlas;
  std::uint32_t exact_font_bytes;
  std::uint32_t minimum_font_size;
  std::uint32_t minimum_line_height;
};

AssetSpec Animation(InstalledAssetKey key, const char* metadata_path,
                    const char* id, std::uint32_t width,
                    std::uint32_t height, std::uint32_t frames,
                    const char* atlas) {
  std::string meta(metadata_path);
  constexpr std::string_view suffix = ".meta.toml";
  std::string data = meta.substr(0, meta.size() - suffix.size()) + ".png";
  return {key, InstalledAssetKind::kAnimation, std::move(data),
          std::move(meta), id, width, height, frames, atlas, 0, 0, 0};
}

AssetSpec Font(InstalledAssetKey key, const char* data_path, const char* id,
               std::uint32_t bytes, std::uint32_t minimum_size,
               std::uint32_t line_height) {
  std::string data(data_path);
  constexpr std::string_view suffix = ".ttf";
  std::string meta = data.substr(0, data.size() - suffix.size()) + ".meta.toml";
  return {key, InstalledAssetKind::kFont, std::move(data), std::move(meta), id,
          0, 0, 0, "", bytes, minimum_size, line_height};
}

std::vector<AssetSpec> ProductionWhitelist() {
  using K = InstalledAssetKey;
  return {
      Animation(K::kJournalBackplate,
          "assets/animations/UI NEW/Journal/spr_ui_journal_backplate.meta.toml",
          "4702779637b29d89", 410, 234, 1, "UI"),
      Animation(K::kInventoryLeftPage,
          "assets/animations/UI NEW/Journal/spr_ui_journal_book_page_layout_inventory_left.meta.toml",
          "cbb7c2a856e09a0f", 189, 218, 1, "UI"),
      Animation(K::kGenericBox,
          "assets/animations/UI NEW/Generic/Box/spr_ui_generic_box_main.meta.toml",
          "783a48fad2e7d292", 4, 4, 9, "UI"),
      Animation(K::kGenericButton,
          "assets/animations/UI NEW/Generic/Button/spr_ui_button_main.meta.toml",
          "7e8cc6f0c0b60e02", 3, 3, 9, "UI"),
      Animation(K::kFontLut,
          "assets/animations/UI NEW/Fonts/spr_ui_generic_font_lut.meta.toml",
          "3cb7abe98f97949a", 21, 256, 1, "Default"),
      Animation(K::kHealthBackplate,
          "assets/animations/UI NEW/HUD/spr_ui_hud_health_health_bar_backplate.meta.toml",
          "c7441fd5f4ef1a65", 4, 5, 9, "UI"),
      Animation(K::kHealthFill,
          "assets/animations/UI NEW/HUD/spr_ui_hud_health_health_bar.meta.toml",
          "10280c00b1f329d1", 1, 2, 9, "UI"),
      Animation(K::kStaminaFill,
          "assets/animations/UI NEW/HUD/spr_ui_hud_health_stamina_bar.meta.toml",
          "506224c49c069c78", 1, 2, 9, "UI"),
      Animation(K::kManaOrbOn,
          "assets/animations/UI NEW/HUD/spr_ui_hud_health_mana_ball_on.meta.toml",
          "6e1cecd9b2b5e889", 9, 9, 1, "UI"),
      Animation(K::kInfoHudTop,
          "assets/animations/UI NEW/HUD/spr_ui_hud_info_backplate_top.meta.toml",
          "b58027d2a47a0589", 8, 8, 9, "UI"),
      Animation(K::kSunnyWeather,
          "assets/animations/UI NEW/HUD/spr_ui_hud_info_backplate_weather_icon_sunny.meta.toml",
          "7fe1a1e4c607da4f", 10, 10, 1, "UI"),
      Animation(K::kSpringSeason,
          "assets/animations/UI NEW/HUD/spr_ui_hud_info_backplate_season_icon_spring.meta.toml",
          "b967c1510254c266", 10, 10, 1, "UI"),
      Animation(K::kCurrency,
          "assets/animations/UI NEW/HUD/spr_ui_hud_info_currency_icon.meta.toml",
          "a7d6f7b8c91f9bf", 8, 8, 1, "UI"),
      Animation(K::kEssence,
          "assets/animations/UI NEW/HUD/spr_ui_hud_info_essence_icon.meta.toml",
          "f699cc1a861adfbf", 8, 8, 1, "UI"),
      Animation(K::kStatusEffectBarBack,
          "assets/animations/UI/Status Effect/spr_ui_statuseffect_bar_back.meta.toml",
          "141e838035dd947d", 1, 1, 9, "UI"),
      Animation(K::kMapGrid,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_grid.meta.toml",
          "dda7256a8d29cbc9", 362, 202, 1, "UI"),
      Animation(K::kBeachMap,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_beach.meta.toml",
          "e686cfe8861007c6", 339, 179, 1, "UI"),
      Animation(K::kDeepWoodsMap,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_deep_woods.meta.toml",
          "bd493660489029fe", 339, 179, 1, "UI"),
      Animation(K::kDragonswornGladeMap,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_dragonsworn_glade.meta.toml",
          "5833518e925cdcfd", 339, 179, 1, "UI"),
      Animation(K::kEasternRoadMap,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_eastern_road.meta.toml",
          "b9f440f06cb4489b", 339, 179, 1, "UI"),
      Animation(K::kFarmMap,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_farm.meta.toml",
          "462b8dcb60f81d53", 339, 179, 1, "UI"),
      Animation(K::kFarmExpansionMap,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_farm_expansion.meta.toml",
          "1603062b15c61868", 339, 179, 1, "UI"),
      Animation(K::kHaydensFarmMap,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_haydens_farm.meta.toml",
          "16151f90fcce1218", 339, 179, 1, "UI"),
      Animation(K::kNarrowsMap,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_narrows.meta.toml",
          "1adfa585a7a92b3a", 339, 179, 1, "UI"),
      Animation(K::kSummitMap,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_summit.meta.toml",
          "dc26a1e2a17a3730", 339, 179, 1, "UI"),
      Animation(K::kTownMap,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_town.meta.toml",
          "f2ab438aecf145b9", 339, 179, 1, "UI"),
      Animation(K::kWesternRuinsMap,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_western_ruins.meta.toml",
          "98d36aece77f70af", 339, 179, 1, "UI"),
      Animation(K::kPlayerMapMarker,
          "assets/animations/UI NEW/Generic/Icons/NPC Small/spr_ui_generic_icon_npc_small_player.meta.toml",
          "ac367f7789670d79", 11, 11, 9, "UI"),
      Animation(K::kFarmMapTab,
          "assets/animations/UI NEW/Journal/Map/spr_ui_map_tab_icon_farm.meta.toml",
          "960f32a8bec650e7", 18, 17, 1, "UI"),
      Animation(K::kRelationshipHeader,
          "assets/animations/UI NEW/Journal/Relationship/spr_ui_journal_relationship_header_icon.meta.toml",
          "bb5935b153cf25d6", 14, 14, 1, "UI"),
      Animation(K::kRelationshipHearts,
          "assets/animations/UI NEW/Journal/Relationship/spr_ui_journal_relationship_heart_level.meta.toml",
          "100fb6ecd906e43d", 39, 15, 11, "UI"),
      Animation(K::kAdelineSpringNeutral,
          "assets/animations/NPCs/Adeline/Portraits/Spring/spr_portrait_adeline_spring_neutral.meta.toml",
          "5e2770bc75d292ac", 296, 180, 2, "PortraitsSpring"),
      Animation(K::kTurnipIcon,
          "assets/animations/Item Icons/Crops/spr_ui_item_turnip.meta.toml",
          "8ef4b5af2f84f924", 18, 18, 1, "UI"),
      Animation(K::kAlmanacHeader,
          "assets/animations/UI NEW/Journal/Almanac/spr_ui_journal_almanac_header_icon.meta.toml",
          "56693bbcd6fa27d5", 14, 14, 1, "UI"),
      Animation(K::kAlmanacCrops,
          "assets/animations/UI NEW/Journal/Almanac/spr_ui_journal_almanac_icon_crops.meta.toml",
          "7bfb693d83356591", 14, 13, 1, "UI"),
      Animation(K::kFarmingSkill,
          "assets/animations/UI NEW/Generic/Icons/Skill/spr_ui_skill_icon_farming.meta.toml",
          "69e8d6b509e8d967", 8, 7, 1, "UI"),
      Animation(K::kGreenSkillProgress,
          "assets/animations/UI NEW/Journal/spr_ui_journal_skill_progress_bar_green.meta.toml",
          "ec261c36807f251e", 96, 7, 95, "UI"),
      Animation(K::kMagicHeader,
          "assets/animations/UI NEW/Journal/Magic/spr_ui_journal_magic_header_icon.meta.toml",
          "48efe0f92476aac5", 14, 14, 1, "UI"),
      Animation(K::kMagicCard,
          "assets/animations/UI NEW/Journal/Magic/spr_ui_journal_magic_card_backplate.meta.toml",
          "5d1da79668dc2db8", 74, 100, 1, "UI"),
      Animation(K::kRestoreCardIcon,
          "assets/animations/UI NEW/Journal/Magic/spr_ui_journal_magic_restore_card_icon.meta.toml",
          "5e00c81ec45d8250", 14, 12, 1, "UI"),
      Animation(K::kAnimalHeader,
          "assets/animations/UI NEW/Journal/Animals/spr_ui_journal_animal_header_icon.meta.toml",
          "6ec145c8bd549b25", 14, 14, 1, "UI"),
      Animation(K::kAnimalHearts,
          "assets/animations/UI NEW/Journal/Animals/spr_ui_journal_animal_heart_level.meta.toml",
          "860055ff70f22c43", 39, 15, 11, "UI"),
      Animation(K::kAnimalPreviewBox,
          "assets/animations/UI NEW/Journal/Animals/spr_ui_journal_animal_preview_box.meta.toml",
          "2e386f0eefb7c39c", 2, 2, 9, "UI"),
      Animation(K::kChickenFemaleIcon,
          "assets/animations/UI NEW/Ranching/spr_ui_icon_animal_chicken_basic_female.meta.toml",
          "f4c6d242e143867d", 18, 18, 1, "UI"),
      Animation(K::kMuseumOn,
          "assets/animations/UI NEW/Generic/Icons/spr_ui_generic_icon_museum_on.meta.toml",
          "c14f40d56e1c0072", 19, 10, 1, "UI"),
      Animation(K::kRenown,
          "assets/animations/UI NEW/Journal/Inventory/spr_ui_journal_inventory_renown_icon.meta.toml",
          "348f9ca21c710578", 10, 8, 1, "UI"),
      Font(K::kBirdseedFont, "assets/fonts/fnt_mistria_birdseed.ttf",
           "db8119e8fc1f60f3", 25848, 16, 13),
      Font(K::kPixelMPlusFont, "assets/fonts/fnt_pixel_m_plus.ttf",
           "925bcca2087de7fc", 1274396, 12, 14),
      Font(K::kCubic11Font, "assets/fonts/fnt_cubic_11.ttf",
           "1a071ee8ccda4cf2", 2768240, 12, 14),
      Font(K::kZpixFont, "assets/fonts/fnt_zpix.ttf",
           "83a694c47ebc1031", 7179288, 12, 12),
      Font(K::kSilverFont, "assets/fonts/fnt_silver.ttf",
           "bbefb270d4dd141b", 3740448, 19, 14),
      Font(K::kNosutaruFont, "assets/fonts/fnt_nosutaru.ttf",
           "2041ade250fa9f07", 2952228, 12, 14),
  };
}

struct TrustProfile {
  std::string original_sha256;
  std::uint64_t original_size;
  std::uint64_t original_entries;
  std::string patched_sha256;
  std::uint64_t patched_size;
  std::uint64_t patched_entries;
  std::string patch_manifest_sha256;
  std::string patched_game_sha256;
  std::string bridge_sha256;
};

TrustProfile ProductionTrust() {
  return {kOriginalArchiveSha256, kOriginalArchiveSize,
          kOriginalArchiveEntries, kPatchedArchiveSha256,
          kPatchedArchiveSize, kPatchedArchiveEntries,
          kPatchManifestSha256, kPatchedGameSha256, kBridgeSha256};
}

std::wstring Join(const std::wstring& directory, const wchar_t* name) {
  if (directory.empty()) return {};
  std::wstring result = directory;
  if (result.back() != L'\\' && result.back() != L'/') result.push_back(L'\\');
  result.append(name);
  return result;
}

std::uint16_t U16(const std::uint8_t* data) {
  return static_cast<std::uint16_t>(data[0]) |
      (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t U32(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) |
      (static_cast<std::uint32_t>(data[1]) << 8U) |
      (static_cast<std::uint32_t>(data[2]) << 16U) |
      (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::uint64_t U64(const std::uint8_t* data) {
  return static_cast<std::uint64_t>(U32(data)) |
      (static_cast<std::uint64_t>(U32(data + 4)) << 32U);
}

std::uint32_t Big32(const std::uint8_t* data) {
  return (static_cast<std::uint32_t>(data[0]) << 24U) |
      (static_cast<std::uint32_t>(data[1]) << 16U) |
      (static_cast<std::uint32_t>(data[2]) << 8U) |
      static_cast<std::uint32_t>(data[3]);
}

bool AddWithin(std::uint64_t left, std::uint64_t right, std::uint64_t limit,
               std::uint64_t* output = nullptr) {
  if (left > limit || right > limit - left) return false;
  if (output) *output = left + right;
  return true;
}

bool ReadAt(HANDLE file, std::uint64_t file_size, std::uint64_t offset,
            void* output, std::size_t bytes) {
  if (!output || bytes > std::numeric_limits<DWORD>::max() ||
      !AddWithin(offset, bytes, file_size)) return false;
  LARGE_INTEGER position = {};
  position.QuadPart = static_cast<LONGLONG>(offset);
  if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) return false;
  DWORD read = 0;
  return ReadFile(file, output, static_cast<DWORD>(bytes), &read, nullptr) &&
      read == bytes;
}

bool HashHandle(HANDLE file, std::uint64_t file_size, std::string* output) {
  if (!output) return false;
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_size = 0;
  DWORD transferred = 0;
  bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                        nullptr, 0) >= 0 &&
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&object_size),
                        sizeof(object_size), &transferred, 0) >= 0;
  std::vector<std::uint8_t> object(object_size);
  if (ok) {
    ok = BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                          nullptr, 0, 0) >= 0;
  }
  std::vector<std::uint8_t> buffer(256 * 1024);
  for (std::uint64_t offset = 0; ok && offset < file_size;) {
    const std::size_t amount = static_cast<std::size_t>(
        std::min<std::uint64_t>(buffer.size(), file_size - offset));
    ok = ReadAt(file, file_size, offset, buffer.data(), amount) &&
        BCryptHashData(hash, buffer.data(), static_cast<ULONG>(amount), 0) >= 0;
    offset += amount;
  }
  std::uint8_t digest[32] = {};
  if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!ok) return false;
  char hex[65] = {};
  for (std::size_t index = 0; index < sizeof(digest); ++index) {
    std::snprintf(hex + index * 2, sizeof(hex) - index * 2, "%02x",
                  digest[index]);
  }
  *output = hex;
  return true;
}

bool FileSize(HANDLE file, std::uint64_t* output) {
  LARGE_INTEGER size = {};
  if (!output || !GetFileSizeEx(file, &size) || size.QuadPart < 0) return false;
  *output = static_cast<std::uint64_t>(size.QuadPart);
  return true;
}

bool ExistsRegular(const std::wstring& path, bool* exists) {
  if (!exists) return false;
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      *exists = false;
      return true;
    }
    return false;
  }
  *exists = true;
  return !(attributes & FILE_ATTRIBUTE_DIRECTORY) &&
      !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

HANDLE OpenReadLocked(const std::wstring& path) {
  return CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                     OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
}

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size) {
  std::uint32_t crc = 0xffffffffU;
  for (std::size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xedb88320U &
          static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1U)));
    }
  }
  return ~crc;
}

bool SafeArchiveName(const std::string& name) {
  if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos ||
      name.find('\0') != std::string::npos || name.find(':') != std::string::npos) {
    return false;
  }
  std::size_t begin = 0;
  while (begin < name.size()) {
    const std::size_t end = name.find('/', begin);
    const std::size_t count =
        (end == std::string::npos ? name.size() : end) - begin;
    if (count == 0 || (count == 1 && name[begin] == '.') ||
        (count == 2 && name[begin] == '.' && name[begin + 1] == '.')) {
      return false;
    }
    if (end == std::string::npos) break;
    begin = end + 1;
    if (begin == name.size()) return true;
  }
  return true;
}

bool ExactLowerHexId(const std::string& value) {
  return value.size() == 16 &&
      std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');
      });
}

bool ExactLogicalAnimationId(const std::string& value) {
  return value.size() >= 4 && value.size() <= 192 &&
      std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '_';
      });
}

constexpr std::size_t kDynamicClassCount = 6;

std::size_t DynamicClassIndex(InstalledDynamicAssetClass value) {
  return static_cast<std::size_t>(value);
}

int DynamicClassForPath(const std::string& path) {
  if (path.starts_with("assets/animations/Item Icons/")) {
    return static_cast<int>(InstalledDynamicAssetClass::kItemIcon);
  }
  if (path.starts_with("assets/animations/NPCs/") &&
      path.find("/Portraits/") != std::string::npos) {
    return static_cast<int>(InstalledDynamicAssetClass::kNpcPortrait);
  }
  if (path.starts_with(
          "assets/animations/UI NEW/Generic/Icons/NPC Small/")) {
    return static_cast<int>(InstalledDynamicAssetClass::kNpcMapIcon);
  }
  if (path.starts_with("assets/animations/UI NEW/Ranching/") ||
      path.starts_with("assets/animations/UI NEW/Pets/")) {
    return static_cast<int>(InstalledDynamicAssetClass::kAnimalIcon);
  }
  if (path.starts_with(
          "assets/animations/UI NEW/Generic/Icons/Skill/") ||
      path.starts_with("assets/animations/UI NEW/Skills/")) {
    return static_cast<int>(InstalledDynamicAssetClass::kSkillIcon);
  }
  if (path.starts_with("assets/animations/UI NEW/Journal/Magic/")) {
    return static_cast<int>(InstalledDynamicAssetClass::kSpellIcon);
  }
  return -1;
}

bool DynamicAtlasAllowed(InstalledDynamicAssetClass asset_class,
                         const std::string& atlas) {
  if (asset_class == InstalledDynamicAssetClass::kNpcPortrait) {
    return atlas.starts_with("Portraits") && atlas.size() <= 32 &&
        std::all_of(atlas.begin(), atlas.end(), [](char character) {
          return (character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '_';
        });
  }
  return atlas == "UI";
}

std::string FoldAscii(const std::string& value) {
  std::string result = value;
  for (char& character : result) {
    if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
  }
  return result;
}

std::string Trim(std::string value) {
  const auto whitespace = [](unsigned char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
  };
  while (!value.empty() && whitespace(value.front())) value.erase(value.begin());
  while (!value.empty() && whitespace(value.back())) value.pop_back();
  return value;
}

bool ParseUnsigned(const std::string& text, std::uint64_t* output) {
  if (!output || text.empty()) return false;
  std::uint64_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size()) {
    return false;
  }
  *output = value;
  return true;
}

bool ParseQuoted(const std::string& text, std::string* output) {
  if (!output || text.size() < 2 || text.front() != '"' || text.back() != '"') {
    return false;
  }
  output->clear();
  for (std::size_t index = 1; index + 1 < text.size(); ++index) {
    const char character = text[index];
    if (character == '\\' || static_cast<unsigned char>(character) < 0x20U) {
      return false;
    }
    output->push_back(character);
  }
  return true;
}

bool ParseFlatJson(const std::string& json,
                   std::unordered_map<std::string, std::string>* values) {
  if (!values || json.size() > 64 * 1024) return false;
  values->clear();
  std::size_t cursor = 0;
  const auto skip = [&]() {
    while (cursor < json.size() &&
           (json[cursor] == ' ' || json[cursor] == '\t' ||
            json[cursor] == '\r' || json[cursor] == '\n')) ++cursor;
  };
  const auto string_token = [&](std::string* output) {
    if (cursor >= json.size() || json[cursor++] != '"') return false;
    output->clear();
    while (cursor < json.size() && json[cursor] != '"') {
      const char character = json[cursor++];
      if (character == '\\' || static_cast<unsigned char>(character) < 0x20U) {
        return false;
      }
      output->push_back(character);
    }
    if (cursor >= json.size()) return false;
    ++cursor;
    return true;
  };
  skip();
  if (cursor >= json.size() || json[cursor++] != '{') return false;
  skip();
  if (cursor < json.size() && json[cursor] == '}') return false;
  for (;;) {
    std::string key;
    if (!string_token(&key) || values->contains(key)) return false;
    skip();
    if (cursor >= json.size() || json[cursor++] != ':') return false;
    skip();
    std::string value;
    if (cursor < json.size() && json[cursor] == '"') {
      if (!string_token(&value)) return false;
    } else {
      const std::size_t begin = cursor;
      while (cursor < json.size() && json[cursor] != ',' && json[cursor] != '}' &&
             json[cursor] != ' ' && json[cursor] != '\t' &&
             json[cursor] != '\r' && json[cursor] != '\n') ++cursor;
      if (cursor == begin) return false;
      value = json.substr(begin, cursor - begin);
      const bool decimal = std::all_of(value.begin(), value.end(),
          [](char character) { return character >= '0' && character <= '9'; });
      if (!decimal && value != "true" && value != "false" && value != "null") {
        return false;
      }
    }
    values->emplace(std::move(key), std::move(value));
    skip();
    if (cursor >= json.size()) return false;
    if (json[cursor] == '}') {
      ++cursor;
      skip();
      return cursor == json.size();
    }
    if (json[cursor++] != ',') return false;
    skip();
  }
}

bool JsonEquals(const std::unordered_map<std::string, std::string>& values,
                const char* key, const std::string& expected) {
  const auto found = values.find(key);
  return found != values.end() && found->second == expected;
}

bool JsonNumber(const std::unordered_map<std::string, std::string>& values,
                const char* key, std::uint64_t expected) {
  const auto found = values.find(key);
  std::uint64_t value = 0;
  return found != values.end() && ParseUnsigned(found->second, &value) &&
      value == expected;
}

}  // namespace

class InstalledAssetProvider::Impl {
 public:
  explicit Impl(InstalledAssetProviderConfig config) : config_(config) {
    config_.max_cached_items =
        std::clamp<std::size_t>(config_.max_cached_items, 1, 128);
    config_.max_cached_bytes = std::clamp<std::size_t>(
        config_.max_cached_bytes, 64 * 1024, 64 * 1024 * 1024);
    config_.max_data_bytes = std::clamp<std::size_t>(
        config_.max_data_bytes, 1024, 16 * 1024 * 1024);
    config_.max_metadata_bytes = std::clamp<std::size_t>(
        config_.max_metadata_bytes, 256, 256 * 1024);
    config_.max_central_directory_bytes = std::clamp<std::uint64_t>(
        config_.max_central_directory_bytes, 1024, 128 * 1024 * 1024);
    config_.max_archive_entries = std::clamp<std::uint64_t>(
        config_.max_archive_entries, 1, 250000);
    config_.max_archive_name_bytes = std::clamp<std::size_t>(
        config_.max_archive_name_bytes, 32, 4096);
    config_.max_dynamic_image_pixels = std::clamp<std::uint64_t>(
        config_.max_dynamic_image_pixels, 1, 64ULL * 1024ULL * 1024ULL);
    config_.max_dynamic_image_dimension = std::clamp<std::uint32_t>(
        config_.max_dynamic_image_dimension, 1, 32768);
  }

  ~Impl() { Close(); }

  bool OpenModule(HMODULE module) {
    if (!module) {
      SetError("mod DLL handle is required for adjacent asset resolution");
      return false;
    }
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(
        module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) {
      SetError("could not resolve mod DLL path");
      return false;
    }
    std::wstring directory(path.data(), length);
    const std::size_t slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
      SetError("mod DLL path has no directory");
      return false;
    }
    directory.resize(slash);
    return Open(directory, ProductionTrust(), ProductionWhitelist(), true);
  }

#ifdef FOMDS_ASSET_PROVIDER_TESTING
  bool OpenSynthetic(
      const std::wstring& directory,
      const InstalledAssetProvider::SyntheticTrustProfile& input_trust,
      const std::vector<InstalledAssetProvider::SyntheticAssetSpec>& input_specs) {
    TrustProfile trust{input_trust.original_sha256,
                       input_trust.original_size,
                       input_trust.original_entries,
                       input_trust.patched_sha256,
                       input_trust.patched_size,
                       input_trust.patched_entries,
                       input_trust.patch_manifest_sha256,
                       input_trust.patched_game_sha256,
                       input_trust.bridge_sha256};
    std::vector<AssetSpec> specs;
    specs.reserve(input_specs.size());
    for (const auto& value : input_specs) {
      specs.push_back({value.key, value.kind, value.data_path,
                       value.metadata_path, value.asset_id,
                       value.frame_width, value.frame_height,
                       value.frame_count, value.atlas, value.font_bytes,
                       value.minimum_font_size,
                       value.minimum_line_height});
    }
    return Open(directory, trust, std::move(specs), false);
  }

  bool OpenExactDirectory(const std::wstring& directory) {
    return Open(directory, ProductionTrust(), ProductionWhitelist(), true);
  }
#endif

  void Close() {
    std::lock_guard<std::mutex> guard(mutex_);
    CloseLocked();
  }

  bool IsOpen() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return open_;
  }

  InstalledAssetResult Load(
      InstalledAssetKey key,
      std::shared_ptr<const InstalledAssetResource>* output) {
    if (!output) return InstalledAssetResult::kIoError;
    output->reset();
    std::lock_guard<std::mutex> guard(mutex_);
    if (!open_) {
      SetErrorLocked("asset provider is not open");
      return InstalledAssetResult::kNotOpen;
    }
    const std::uint16_t numeric = static_cast<std::uint16_t>(key);
    const auto cached = cache_.find(numeric);
    if (cached != cache_.end()) {
      cached->second.last_use = ++cache_clock_;
      ++stats_.hits;
      *output = cached->second.resource;
      return InstalledAssetResult::kOk;
    }
    ++stats_.misses;
    const AssetSpec* spec = nullptr;
    for (const AssetSpec& candidate : specs_) {
      if (candidate.key == key) {
        spec = &candidate;
        break;
      }
    }
    if (!spec) {
      SetErrorLocked("asset key is not in the exact-build whitelist");
      return InstalledAssetResult::kNotWhitelisted;
    }

    const auto data_entry = entries_.find(spec->data_path);
    const auto meta_entry = entries_.find(spec->metadata_path);
    if (data_entry == entries_.end() || meta_entry == entries_.end()) {
      SetErrorLocked("whitelisted data/metadata pair is missing");
      return InstalledAssetResult::kMissingPair;
    }
    std::vector<std::uint8_t> data;
    std::vector<std::uint8_t> metadata_bytes;
    InstalledAssetResult result = ReadEntry(data_entry->second,
                                            config_.max_data_bytes, &data);
    if (result != InstalledAssetResult::kOk) return result;
    result = ReadEntry(meta_entry->second, config_.max_metadata_bytes,
                       &metadata_bytes);
    if (result != InstalledAssetResult::kOk) return result;

    auto resource = std::make_shared<InstalledAssetResource>();
    resource->key = key;
    resource->data = std::move(data);
    resource->metadata_text.assign(
        reinterpret_cast<const char*>(metadata_bytes.data()),
        metadata_bytes.size());
    result = ValidateResource(*spec, resource.get());
    if (result != InstalledAssetResult::kOk) return result;

    const std::size_t bytes = resource->data.size() +
        resource->metadata_text.size();
    if (bytes > config_.max_cached_bytes) {
      SetErrorLocked("asset pair exceeds total cache byte limit");
      return InstalledAssetResult::kCacheFull;
    }
    while (cache_.size() >= config_.max_cached_items ||
           cache_bytes_ > config_.max_cached_bytes - bytes) {
      auto victim = cache_.end();
      for (auto candidate = cache_.begin(); candidate != cache_.end();
           ++candidate) {
        if (candidate->second.resource.use_count() != 1) continue;
        if (victim == cache_.end() ||
            candidate->second.last_use < victim->second.last_use) {
          victim = candidate;
        }
      }
      if (victim == cache_.end()) {
        SetErrorLocked("all bounded cache entries are retained by renderer");
        return InstalledAssetResult::kCacheFull;
      }
      cache_bytes_ -= victim->second.bytes;
      cache_.erase(victim);
      ++stats_.evictions;
    }
    CacheEntry cache_entry;
    cache_entry.resource = resource;
    cache_entry.bytes = bytes;
    cache_entry.last_use = ++cache_clock_;
    cache_.emplace(numeric, std::move(cache_entry));
    cache_bytes_ += bytes;
    stats_.items = cache_.size();
    stats_.bytes = cache_bytes_;
    *output = std::move(resource);
    last_error_.clear();
    return InstalledAssetResult::kOk;
  }

  InstalledAssetResult LoadItemIcon(
      const InstalledItemIconRef& source,
      std::shared_ptr<const InstalledAssetResource>* output);
  InstalledAssetResult LoadDynamicAnimation(
      const InstalledDynamicAnimationRef& source,
      std::shared_ptr<const InstalledAssetResource>* output);

  InstalledAssetCacheStats Stats() const {
    std::lock_guard<std::mutex> guard(mutex_);
    InstalledAssetCacheStats result = stats_;
    result.items = cache_.size();
    result.bytes = cache_bytes_;
    return result;
  }

  std::string Error() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return last_error_;
  }

 private:
  struct Entry {
    std::uint16_t flags = 0;
    std::uint16_t method = 0;
    std::uint32_t crc32 = 0;
    std::uint64_t compressed_size = 0;
    std::uint64_t uncompressed_size = 0;
    std::uint64_t local_offset = 0;
    std::string name;
  };

  struct CacheEntry {
    std::shared_ptr<const InstalledAssetResource> resource;
    std::size_t bytes = 0;
    std::uint64_t last_use = 0;
  };

  void CloseLocked() {
    open_ = false;
    entries_.clear();
    specs_.clear();
    for (auto& resolver : dynamic_paths_) resolver.clear();
    cache_.clear();
    cache_bytes_ = 0;
    cache_clock_ = 0;
    stats_ = {};
    archive_size_ = 0;
    if (archive_ != INVALID_HANDLE_VALUE) {
      CloseHandle(archive_);
      archive_ = INVALID_HANDLE_VALUE;
    }
  }

  void SetError(const char* error) {
    std::lock_guard<std::mutex> guard(mutex_);
    SetErrorLocked(error);
  }

  void SetErrorLocked(const char* error) {
    last_error_ = error ? error : "unknown asset-provider error";
  }

  bool Open(const std::wstring& directory, const TrustProfile& trust,
            std::vector<AssetSpec> specs, bool verify_executable) {
    std::lock_guard<std::mutex> guard(mutex_);
    CloseLocked();
    last_error_.clear();
    const DWORD directory_attributes = GetFileAttributesW(directory.c_str());
    if (directory.empty() || directory_attributes == INVALID_FILE_ATTRIBUTES ||
        !(directory_attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (directory_attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
      SetErrorLocked("game directory is missing, non-directory, or reparse point");
      return false;
    }
    if (specs.empty() || trust.original_sha256.size() != 64 ||
        trust.original_size == 0 || trust.original_entries == 0) {
      SetErrorLocked("asset trust profile is incomplete");
      return false;
    }
    std::unordered_set<std::uint16_t> keys;
    std::unordered_set<std::string> paths;
    for (const AssetSpec& spec : specs) {
      if (!keys.insert(static_cast<std::uint16_t>(spec.key)).second ||
          !paths.insert(spec.data_path).second ||
          !paths.insert(spec.metadata_path).second ||
          !SafeArchiveName(spec.data_path) ||
          !SafeArchiveName(spec.metadata_path)) {
        SetErrorLocked("asset whitelist contains duplicate or unsafe entries");
        return false;
      }
    }

    const std::wstring live_path = Join(directory, kLiveArchiveName);
    const std::wstring backup_path = Join(directory, kBackupArchiveName);
    const std::wstring owner_path = Join(directory, kOwnerName);
    const std::wstring manifest_path = Join(directory, kDeploymentManifestName);
    bool live = false;
    bool backup = false;
    bool owner = false;
    bool manifest = false;
    if (!ExistsRegular(live_path, &live) ||
        !ExistsRegular(backup_path, &backup) ||
        !ExistsRegular(owner_path, &owner) ||
        !ExistsRegular(manifest_path, &manifest)) {
      SetErrorLocked("managed asset path is unreadable, non-regular, or reparse point");
      return false;
    }
    const bool fresh = live && !backup && !owner && !manifest;
    const bool owned = live && backup && owner && manifest;
    if (!fresh && !owned) {
      SetErrorLocked("unknown or partial installed asset state");
      return false;
    }

    if (verify_executable) {
      const std::wstring executable_path = Join(directory, L"FieldsOfMistria.exe");
      bool executable_exists = false;
      if (!ExistsRegular(executable_path, &executable_exists) ||
          !executable_exists) {
        SetErrorLocked("exact game executable is missing or unsafe");
        return false;
      }
      HANDLE executable = OpenReadLocked(executable_path);
      std::uint64_t executable_size = 0;
      std::string executable_hash;
      const bool valid = executable != INVALID_HANDLE_VALUE &&
          FileSize(executable, &executable_size) &&
          executable_size == 13657696ULL &&
          HashHandle(executable, executable_size, &executable_hash) &&
          executable_hash == kExpectedExecutableSha256;
      if (executable != INVALID_HANDLE_VALUE) CloseHandle(executable);
      if (!valid) {
        SetErrorLocked("exact FieldsOfMistria.exe identity mismatch");
        return false;
      }
    }

    HANDLE live_handle = OpenReadLocked(live_path);
    if (live_handle == INVALID_HANDLE_VALUE) {
      SetErrorLocked("could not lock installed assets.zip for read");
      return false;
    }
    std::uint64_t live_size = 0;
    std::string live_hash;
    bool valid = FileSize(live_handle, &live_size) &&
        HashHandle(live_handle, live_size, &live_hash);
    if (!valid) {
      CloseHandle(live_handle);
      SetErrorLocked("could not identify installed assets.zip");
      return false;
    }

    HANDLE source_handle = INVALID_HANDLE_VALUE;
    std::uint64_t source_size = 0;
    if (fresh) {
      if (live_size != trust.original_size ||
          live_hash != trust.original_sha256) {
        CloseHandle(live_handle);
        SetErrorLocked("fresh assets.zip does not match audited original");
        return false;
      }
      source_handle = live_handle;
      source_size = live_size;
    } else {
      HANDLE backup_handle = OpenReadLocked(backup_path);
      HANDLE owner_handle = OpenReadLocked(owner_path);
      HANDLE manifest_handle = OpenReadLocked(manifest_path);
      if (backup_handle == INVALID_HANDLE_VALUE ||
          owner_handle == INVALID_HANDLE_VALUE ||
          manifest_handle == INVALID_HANDLE_VALUE) {
        if (backup_handle != INVALID_HANDLE_VALUE) CloseHandle(backup_handle);
        if (owner_handle != INVALID_HANDLE_VALUE) CloseHandle(owner_handle);
        if (manifest_handle != INVALID_HANDLE_VALUE) CloseHandle(manifest_handle);
        CloseHandle(live_handle);
        SetErrorLocked("could not lock complete owned asset chain");
        return false;
      }
      std::uint64_t backup_size = 0;
      std::uint64_t owner_size = 0;
      std::uint64_t manifest_size = 0;
      std::string backup_hash;
      std::string owner_hash;
      std::vector<std::uint8_t> owner_bytes;
      std::vector<std::uint8_t> manifest_bytes;
      valid = FileSize(backup_handle, &backup_size) &&
          backup_size == trust.original_size &&
          HashHandle(backup_handle, backup_size, &backup_hash) &&
          backup_hash == trust.original_sha256 &&
          live_size == trust.patched_size && live_hash == trust.patched_sha256 &&
          FileSize(owner_handle, &owner_size) && owner_size > 0 && owner_size < 256 &&
          HashHandle(owner_handle, owner_size, &owner_hash) &&
          FileSize(manifest_handle, &manifest_size) &&
          manifest_size > 0 && manifest_size <= 64 * 1024;
      if (valid) {
        owner_bytes.resize(static_cast<std::size_t>(owner_size));
        manifest_bytes.resize(static_cast<std::size_t>(manifest_size));
        valid = ReadAt(owner_handle, owner_size, 0, owner_bytes.data(),
                       owner_bytes.size()) &&
            ReadAt(manifest_handle, manifest_size, 0, manifest_bytes.data(),
                   manifest_bytes.size());
      }
      CloseHandle(owner_handle);
      CloseHandle(manifest_handle);
      if (valid) {
        const std::string owner_text(
            reinterpret_cast<const char*>(owner_bytes.data()), owner_bytes.size());
        valid = owner_text == std::string(kAssetOwner) + "\n";
      }
      if (valid) {
        const std::string deployment(
            reinterpret_cast<const char*>(manifest_bytes.data()),
            manifest_bytes.size());
        std::unordered_map<std::string, std::string> values;
        valid = ParseFlatJson(deployment, &values) && values.size() == 18 &&
            JsonNumber(values, "schema", 1) &&
            JsonEquals(values, "owner", kAssetOwner) &&
            JsonEquals(values, "targetExecutable", "FieldsOfMistria.exe") &&
            JsonEquals(values, "targetVersion", "1.0.4") &&
            JsonEquals(values, "targetExecutableSha256", kExpectedExecutableSha256) &&
            JsonEquals(values, "sourceArchiveName", "assets.zip") &&
            JsonEquals(values, "sourceArchiveSha256", trust.original_sha256) &&
            JsonNumber(values, "sourceArchiveSize", trust.original_size) &&
            JsonNumber(values, "sourceArchiveEntries", trust.original_entries) &&
            JsonEquals(values, "originalBackupName",
                       "assets.zip.fomds-original-e55ef6bcaa743000") &&
            JsonNumber(values, "patchToolSchema", 1) &&
            JsonEquals(values, "patchManifestSha256",
                       trust.patch_manifest_sha256) &&
            JsonEquals(values, "patchedArchiveSha256", trust.patched_sha256) &&
            JsonNumber(values, "patchedArchiveSize", trust.patched_size) &&
            JsonNumber(values, "patchedArchiveEntries", trust.patched_entries) &&
            JsonEquals(values, "patchedGameSha256", trust.patched_game_sha256) &&
            JsonEquals(values, "bridgeSha256", trust.bridge_sha256) &&
            JsonEquals(values, "ownerSha256", owner_hash);
      }
      if (!valid) {
        CloseHandle(backup_handle);
        CloseHandle(live_handle);
        SetErrorLocked("owned patched archive trust chain mismatch");
        return false;
      }
      CloseHandle(live_handle);
      source_handle = backup_handle;
      source_size = backup_size;
    }

    archive_ = source_handle;
    archive_size_ = source_size;
    specs_ = std::move(specs);
    if (!IndexArchive(trust.original_entries)) {
      CloseLocked();
      if (last_error_.empty()) SetErrorLocked("ZIP64 archive indexing failed");
      return false;
    }
    open_ = true;
    last_error_.clear();
    return true;
  }

  bool IndexArchive(std::uint64_t expected_entries) {
    constexpr std::uint32_t kEocdSignature = 0x06054b50U;
    constexpr std::uint32_t kZip64LocatorSignature = 0x07064b50U;
    constexpr std::uint32_t kZip64EocdSignature = 0x06064b50U;
    constexpr std::uint32_t kCentralSignature = 0x02014b50U;
    const std::uint64_t tail_size = std::min<std::uint64_t>(archive_size_, 65557);
    if (tail_size < 22) {
      SetErrorLocked("archive is too small for EOCD");
      return false;
    }
    std::vector<std::uint8_t> tail(static_cast<std::size_t>(tail_size));
    const std::uint64_t tail_offset = archive_size_ - tail_size;
    if (!ReadAt(archive_, archive_size_, tail_offset, tail.data(), tail.size())) {
      SetErrorLocked("could not read ZIP tail");
      return false;
    }
    std::size_t eocd_in_tail = std::numeric_limits<std::size_t>::max();
    for (std::size_t cursor = tail.size() - 22;; --cursor) {
      if (U32(tail.data() + cursor) == kEocdSignature &&
          cursor + 22 + U16(tail.data() + cursor + 20) == tail.size()) {
        eocd_in_tail = cursor;
        break;
      }
      if (cursor == 0) break;
    }
    if (eocd_in_tail == std::numeric_limits<std::size_t>::max()) {
      SetErrorLocked("exact terminal ZIP EOCD not found");
      return false;
    }
    const std::uint8_t* eocd = tail.data() + eocd_in_tail;
    if (U16(eocd + 4) != 0 || U16(eocd + 6) != 0) {
      SetErrorLocked("multi-disk ZIP is unsupported");
      return false;
    }
    std::uint64_t entry_count = U16(eocd + 10);
    std::uint64_t central_size = U32(eocd + 12);
    std::uint64_t central_offset = U32(eocd + 16);
    const std::uint64_t eocd_offset = tail_offset + eocd_in_tail;
    if (entry_count == 0xffffU || central_size == 0xffffffffU ||
        central_offset == 0xffffffffU) {
      if (eocd_offset < 20) {
        SetErrorLocked("ZIP64 locator is missing");
        return false;
      }
      std::array<std::uint8_t, 20> locator = {};
      if (!ReadAt(archive_, archive_size_, eocd_offset - locator.size(),
                  locator.data(), locator.size()) ||
          U32(locator.data()) != kZip64LocatorSignature ||
          U32(locator.data() + 4) != 0 || U32(locator.data() + 16) != 1) {
        SetErrorLocked("ZIP64 locator is invalid");
        return false;
      }
      const std::uint64_t zip64_offset = U64(locator.data() + 8);
      std::array<std::uint8_t, 56> zip64 = {};
      if (!ReadAt(archive_, archive_size_, zip64_offset, zip64.data(),
                  zip64.size()) || U32(zip64.data()) != kZip64EocdSignature ||
          U64(zip64.data() + 4) < 44 || U32(zip64.data() + 16) != 0 ||
          U32(zip64.data() + 20) != 0 ||
          U64(zip64.data() + 24) != U64(zip64.data() + 32)) {
        SetErrorLocked("ZIP64 EOCD is invalid");
        return false;
      }
      entry_count = U64(zip64.data() + 32);
      central_size = U64(zip64.data() + 40);
      central_offset = U64(zip64.data() + 48);
    } else if (U16(eocd + 8) != U16(eocd + 10)) {
      SetErrorLocked("classic ZIP entry counts disagree");
      return false;
    }
    if (entry_count != expected_entries ||
        entry_count > config_.max_archive_entries ||
        central_size > config_.max_central_directory_bytes ||
        !AddWithin(central_offset, central_size, eocd_offset)) {
      SetErrorLocked("ZIP central-directory bounds/profile mismatch");
      return false;
    }
    std::vector<std::uint8_t> central(static_cast<std::size_t>(central_size));
    if (!ReadAt(archive_, archive_size_, central_offset, central.data(),
                central.size())) {
      SetErrorLocked("could not read bounded ZIP central directory");
      return false;
    }

    std::unordered_set<std::string> names;
    std::unordered_set<std::string> folded_names;
    names.reserve(static_cast<std::size_t>(entry_count));
    folded_names.reserve(static_cast<std::size_t>(entry_count));
    std::unordered_set<std::string> wanted;
    for (const AssetSpec& spec : specs_) {
      wanted.insert(spec.data_path);
      wanted.insert(spec.metadata_path);
    }
    std::size_t wanted_found = 0;
    std::size_t cursor = 0;
    for (std::uint64_t index = 0; index < entry_count; ++index) {
      if (central.size() - cursor < 46 ||
          U32(central.data() + cursor) != kCentralSignature) {
        SetErrorLocked("central-directory entry header is corrupt");
        return false;
      }
      const std::uint8_t* record = central.data() + cursor;
      const std::uint16_t flags = U16(record + 8);
      const std::uint16_t method = U16(record + 10);
      const std::uint32_t crc = U32(record + 16);
      std::uint64_t compressed = U32(record + 20);
      std::uint64_t uncompressed = U32(record + 24);
      const std::size_t name_length = U16(record + 28);
      const std::size_t extra_length = U16(record + 30);
      const std::size_t comment_length = U16(record + 32);
      std::uint32_t disk = U16(record + 34);
      std::uint64_t local_offset = U32(record + 42);
      std::uint64_t record_size = 0;
      if (name_length == 0 || name_length > config_.max_archive_name_bytes ||
          !AddWithin(46, name_length, central.size() - cursor, &record_size) ||
          !AddWithin(record_size, extra_length, central.size() - cursor,
                     &record_size) ||
          !AddWithin(record_size, comment_length, central.size() - cursor,
                     &record_size)) {
        SetErrorLocked("central-directory variable fields exceed bounds");
        return false;
      }
      const std::string name(
          reinterpret_cast<const char*>(record + 46), name_length);
      if (!SafeArchiveName(name) || !names.insert(name).second ||
          !folded_names.insert(FoldAscii(name)).second) {
        SetErrorLocked("unsafe or duplicate normalized archive name");
        return false;
      }
      const std::uint8_t* extra = record + 46 + name_length;
      const bool need_uncompressed = uncompressed == 0xffffffffU;
      const bool need_compressed = compressed == 0xffffffffU;
      const bool need_offset = local_offset == 0xffffffffU;
      const bool need_disk = disk == 0xffffU;
      if (need_uncompressed || need_compressed || need_offset || need_disk) {
        bool found_zip64 = false;
        for (std::size_t extra_cursor = 0; extra_cursor < extra_length;) {
          if (extra_length - extra_cursor < 4) {
            SetErrorLocked("malformed ZIP extra field");
            return false;
          }
          const std::uint16_t id = U16(extra + extra_cursor);
          const std::size_t length = U16(extra + extra_cursor + 2);
          extra_cursor += 4;
          if (length > extra_length - extra_cursor) {
            SetErrorLocked("ZIP extra field length exceeds entry");
            return false;
          }
          if (id == 0x0001U) {
            const std::uint8_t* zip = extra + extra_cursor;
            std::size_t remaining = length;
            auto take64 = [&](std::uint64_t* value) {
              if (remaining < 8) return false;
              *value = U64(zip);
              zip += 8;
              remaining -= 8;
              return true;
            };
            if ((need_uncompressed && !take64(&uncompressed)) ||
                (need_compressed && !take64(&compressed)) ||
                (need_offset && !take64(&local_offset))) {
              SetErrorLocked("ZIP64 entry values are truncated");
              return false;
            }
            if (need_disk) {
              if (remaining < 4) {
                SetErrorLocked("ZIP64 disk value is truncated");
                return false;
              }
              disk = U32(zip);
            }
            found_zip64 = true;
          }
          extra_cursor += length;
        }
        if (!found_zip64) {
          SetErrorLocked("ZIP64 entry is missing required extra values");
          return false;
        }
      }
      if (disk != 0 || !AddWithin(local_offset, 30, archive_size_)) {
        SetErrorLocked("entry references unsupported disk or local offset");
        return false;
      }
      const bool fixed_asset = wanted.contains(name);
      const bool dynamic_animation_asset =
          DynamicClassForPath(name) >= 0 &&
          (name.ends_with(".png") || name.ends_with(".meta.toml"));
      if (fixed_asset || dynamic_animation_asset) {
        if ((flags & 1U) != 0 || method != 0 || compressed != uncompressed ||
            uncompressed == 0) {
          SetErrorLocked("whitelisted entry is encrypted, compressed, or empty");
          return false;
        }
        const bool is_meta = name.ends_with(".meta.toml");
        const std::size_t limit = is_meta ? config_.max_metadata_bytes
                                          : config_.max_data_bytes;
        if (uncompressed > limit || entries_.contains(name)) {
          SetErrorLocked("whitelisted entry exceeds limit or is duplicated");
          return false;
        }
        entries_.emplace(name, Entry{flags, method, crc, compressed,
                                     uncompressed, local_offset, name});
        if (fixed_asset) ++wanted_found;
      }
      cursor += static_cast<std::size_t>(record_size);
    }
    if (cursor != central.size() || wanted_found != wanted.size()) {
      SetErrorLocked("central directory has trailing data or missing whitelist pair");
      return false;
    }
    for (const auto& pair : entries_) {
      const std::string& path = pair.first;
      if (!path.ends_with(".meta.toml")) continue;
      const int class_value = DynamicClassForPath(path);
      if (class_value < 0) continue;
      constexpr std::string_view suffix = ".meta.toml";
      const std::size_t slash = path.find_last_of('/');
      if (slash == std::string::npos ||
          path.size() <= slash + 1 + suffix.size()) {
        continue;
      }
      const std::string logical_id = path.substr(
          slash + 1, path.size() - slash - 1 - suffix.size());
      if (!ExactLogicalAnimationId(logical_id)) continue;
      const std::string png_path =
          path.substr(0, path.size() - suffix.size()) + ".png";
      if (!entries_.contains(png_path)) continue;
      auto& resolver = dynamic_paths_[static_cast<std::size_t>(class_value)];
      auto [found, inserted] = resolver.emplace(logical_id, path);
      if (!inserted) found->second.clear();
    }
    return true;
  }

  InstalledAssetResult ReadEntry(const Entry& entry, std::size_t limit,
                                 std::vector<std::uint8_t>* output) {
    constexpr std::uint32_t kLocalSignature = 0x04034b50U;
    if (!output || entry.method != 0 || entry.compressed_size !=
            entry.uncompressed_size || entry.uncompressed_size > limit ||
        entry.uncompressed_size > std::numeric_limits<std::size_t>::max()) {
      SetErrorLocked("stored entry violates read limits");
      return InstalledAssetResult::kSizeLimit;
    }
    std::array<std::uint8_t, 30> local = {};
    if (!ReadAt(archive_, archive_size_, entry.local_offset, local.data(),
                local.size())) {
      SetErrorLocked("could not read local ZIP header");
      return InstalledAssetResult::kIoError;
    }
    const std::size_t name_length = U16(local.data() + 26);
    const std::size_t extra_length = U16(local.data() + 28);
    std::uint64_t data_offset = 0;
    if (U32(local.data()) != kLocalSignature ||
        U16(local.data() + 6) != entry.flags ||
        U16(local.data() + 8) != entry.method ||
        name_length != entry.name.size() ||
        !AddWithin(entry.local_offset, local.size(), archive_size_,
                   &data_offset) ||
        !AddWithin(data_offset, name_length, archive_size_, &data_offset) ||
        !AddWithin(data_offset, extra_length, archive_size_, &data_offset) ||
        !AddWithin(data_offset, entry.uncompressed_size, archive_size_)) {
      SetErrorLocked("local ZIP header disagrees with central entry");
      return InstalledAssetResult::kCorruptEntry;
    }
    std::vector<std::uint8_t> local_name(name_length);
    if (!ReadAt(archive_, archive_size_, entry.local_offset + local.size(),
                local_name.data(), local_name.size()) ||
        std::memcmp(local_name.data(), entry.name.data(), name_length) != 0) {
      SetErrorLocked("local ZIP name disagrees with central entry");
      return InstalledAssetResult::kCorruptEntry;
    }
    output->resize(static_cast<std::size_t>(entry.uncompressed_size));
    if (!ReadAt(archive_, archive_size_, data_offset, output->data(),
                output->size())) {
      output->clear();
      SetErrorLocked("could not read stored ZIP payload");
      return InstalledAssetResult::kIoError;
    }
    if (Crc32(output->data(), output->size()) != entry.crc32) {
      output->clear();
      SetErrorLocked("stored ZIP payload CRC mismatch");
      return InstalledAssetResult::kCorruptEntry;
    }
    return InstalledAssetResult::kOk;
  }

  InstalledAssetResult ValidateDynamicAnimation(
      const InstalledDynamicAnimationRef& source,
      InstalledAssetResource* resource) {
    if (!resource || resource->metadata_text.find('\0') != std::string::npos) {
      SetErrorLocked("dynamic item metadata contains embedded NUL");
      return InstalledAssetResult::kMetadataMismatch;
    }
    std::unordered_map<std::string, std::string> values;
    std::size_t cursor = 0;
    while (cursor <= resource->metadata_text.size()) {
      const std::size_t end = resource->metadata_text.find('\n', cursor);
      std::string line = Trim(resource->metadata_text.substr(
          cursor, (end == std::string::npos ? resource->metadata_text.size()
                                            : end) - cursor));
      if (!line.empty() && line.front() != '[' && line.front() != '#') {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
          SetErrorLocked("dynamic item metadata has malformed assignment");
          return InstalledAssetResult::kMetadataMismatch;
        }
        std::string key = Trim(line.substr(0, equals));
        std::string value = Trim(line.substr(equals + 1));
        if ((key == "id" || key == "asset_kind" || key == "frame_size" ||
             key == "frame_len" || key == "atlas") &&
            !values.emplace(std::move(key), std::move(value)).second) {
          SetErrorLocked("dynamic item metadata repeats a validated property");
          return InstalledAssetResult::kMetadataMismatch;
        }
      }
      if (end == std::string::npos) break;
      cursor = end + 1;
    }

    std::string id;
    std::string kind;
    std::string atlas;
    const auto id_value = values.find("id");
    const auto kind_value = values.find("asset_kind");
    const auto frame_value = values.find("frame_size");
    const auto atlas_value = values.find("atlas");
    if (id_value == values.end() || kind_value == values.end() ||
        frame_value == values.end() || atlas_value == values.end() ||
        !ParseQuoted(id_value->second, &id) || !ExactLowerHexId(id) ||
        !ParseQuoted(kind_value->second, &kind) || kind != "Animation" ||
        !ParseQuoted(atlas_value->second, &atlas) ||
        !DynamicAtlasAllowed(source.asset_class, atlas)) {
      SetErrorLocked("dynamic animation metadata ID/kind/atlas mismatch");
      return InstalledAssetResult::kMetadataMismatch;
    }
    const std::string frame = Trim(frame_value->second);
    if (frame.size() < 5 || frame.front() != '[' || frame.back() != ']') {
      SetErrorLocked("dynamic item frame_size is malformed");
      return InstalledAssetResult::kMetadataMismatch;
    }
    const std::string inside = frame.substr(1, frame.size() - 2);
    const std::size_t comma = inside.find(',');
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    if (comma == std::string::npos ||
        !ParseUnsigned(Trim(inside.substr(0, comma)), &width) ||
        !ParseUnsigned(Trim(inside.substr(comma + 1)), &height) || width == 0 ||
        height == 0) {
      SetErrorLocked("dynamic item frame_size is invalid");
      return InstalledAssetResult::kMetadataMismatch;
    }
    std::uint64_t frames = 1;
    const auto frame_len = values.find("frame_len");
    if (frame_len != values.end() &&
        !ParseUnsigned(frame_len->second, &frames)) {
      SetErrorLocked("dynamic item frame_len is malformed");
      return InstalledAssetResult::kMetadataMismatch;
    }
    if (frames == 0 || width > std::numeric_limits<std::uint32_t>::max() ||
        height > std::numeric_limits<std::uint32_t>::max() ||
        width > std::numeric_limits<std::uint32_t>::max() / frames) {
      SetErrorLocked("dynamic item frame geometry overflows");
      return InstalledAssetResult::kMetadataMismatch;
    }
    const std::uint64_t sheet_width = width * frames;
    if (sheet_width > config_.max_dynamic_image_dimension ||
        height > config_.max_dynamic_image_dimension ||
        sheet_width > config_.max_dynamic_image_pixels / height) {
      SetErrorLocked("dynamic item frame geometry exceeds configured bounds");
      return InstalledAssetResult::kSizeLimit;
    }
    static constexpr std::uint8_t kPngSignature[] =
        {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (resource->data.size() < 33 ||
        std::memcmp(resource->data.data(), kPngSignature,
                    sizeof(kPngSignature)) != 0 ||
        Big32(resource->data.data() + 8) != 13 ||
        std::memcmp(resource->data.data() + 12, "IHDR", 4) != 0 ||
        Big32(resource->data.data() + 16) != sheet_width ||
        Big32(resource->data.data() + 20) != height) {
      SetErrorLocked("dynamic item PNG geometry mismatches paired metadata");
      return InstalledAssetResult::kMetadataMismatch;
    }
    resource->metadata.kind = InstalledAssetKind::kAnimation;
    resource->metadata.asset_id = id;
    resource->metadata.frame_width = static_cast<std::uint32_t>(width);
    resource->metadata.frame_height = static_cast<std::uint32_t>(height);
    resource->metadata.frame_count = static_cast<std::uint32_t>(frames);
    resource->metadata.atlas = atlas;
    return InstalledAssetResult::kOk;
  }

  InstalledAssetResult ValidateResource(const AssetSpec& spec,
                                        InstalledAssetResource* resource) {
    if (!resource || resource->metadata_text.find('\0') != std::string::npos) {
      SetErrorLocked("metadata contains embedded NUL");
      return InstalledAssetResult::kMetadataMismatch;
    }
    std::unordered_map<std::string, std::string> values;
    std::size_t cursor = 0;
    while (cursor <= resource->metadata_text.size()) {
      const std::size_t end = resource->metadata_text.find('\n', cursor);
      std::string line = Trim(resource->metadata_text.substr(
          cursor, (end == std::string::npos ? resource->metadata_text.size()
                                            : end) - cursor));
      if (!line.empty() && line.front() != '[' && line.front() != '#') {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
          SetErrorLocked("metadata contains malformed assignment");
          return InstalledAssetResult::kMetadataMismatch;
        }
        std::string key = Trim(line.substr(0, equals));
        std::string value = Trim(line.substr(equals + 1));
        if ((key == "id" || key == "asset_kind" || key == "frame_size" ||
             key == "frame_len" || key == "atlas" ||
             key == "minimum_expected_size" ||
             key == "minimum_expected_line_height" ||
             key == "allow_shading") &&
            !values.emplace(std::move(key), std::move(value)).second) {
          SetErrorLocked("metadata repeats a validated property");
          return InstalledAssetResult::kMetadataMismatch;
        }
      }
      if (end == std::string::npos) break;
      cursor = end + 1;
    }
    std::string id;
    std::string kind;
    const auto id_value = values.find("id");
    const auto kind_value = values.find("asset_kind");
    if (id_value == values.end() || kind_value == values.end() ||
        !ParseQuoted(id_value->second, &id) ||
        !ParseQuoted(kind_value->second, &kind) || id != spec.asset_id) {
      SetErrorLocked("metadata asset ID/kind is missing or mismatched");
      return InstalledAssetResult::kMetadataMismatch;
    }
    resource->metadata.kind = spec.kind;
    resource->metadata.asset_id = id;
    if (spec.kind == InstalledAssetKind::kAnimation) {
      std::string atlas;
      const auto frame_value = values.find("frame_size");
      const auto atlas_value = values.find("atlas");
      if (kind != "Animation" || frame_value == values.end() ||
          atlas_value == values.end() ||
          !ParseQuoted(atlas_value->second, &atlas) || atlas != spec.atlas) {
        SetErrorLocked("animation metadata kind/atlas mismatch");
        return InstalledAssetResult::kMetadataMismatch;
      }
      const std::string frame = Trim(frame_value->second);
      if (frame.size() < 5 || frame.front() != '[' || frame.back() != ']') {
        SetErrorLocked("animation frame_size is malformed");
        return InstalledAssetResult::kMetadataMismatch;
      }
      const std::string inside = frame.substr(1, frame.size() - 2);
      const std::size_t comma = inside.find(',');
      std::uint64_t width = 0;
      std::uint64_t height = 0;
      if (comma == std::string::npos ||
          !ParseUnsigned(Trim(inside.substr(0, comma)), &width) ||
          !ParseUnsigned(Trim(inside.substr(comma + 1)), &height) ||
          width != spec.frame_width || height != spec.frame_height) {
        SetErrorLocked("animation frame_size does not match audited manifest");
        return InstalledAssetResult::kMetadataMismatch;
      }
      std::uint64_t frames = 1;
      const auto frame_len = values.find("frame_len");
      if (frame_len != values.end() &&
          !ParseUnsigned(frame_len->second, &frames)) {
        SetErrorLocked("animation frame_len is malformed");
        return InstalledAssetResult::kMetadataMismatch;
      }
      if (frames != spec.frame_count || frames == 0 ||
          width > std::numeric_limits<std::uint32_t>::max() ||
          height > std::numeric_limits<std::uint32_t>::max()) {
        SetErrorLocked("animation frame count does not match audited manifest");
        return InstalledAssetResult::kMetadataMismatch;
      }
      static constexpr std::uint8_t kPngSignature[] =
          {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
      if (resource->data.size() < 33 ||
          std::memcmp(resource->data.data(), kPngSignature,
                      sizeof(kPngSignature)) != 0 ||
          Big32(resource->data.data() + 8) != 13 ||
          std::memcmp(resource->data.data() + 12, "IHDR", 4) != 0 ||
          width > std::numeric_limits<std::uint32_t>::max() / frames ||
          Big32(resource->data.data() + 16) != width * frames ||
          Big32(resource->data.data() + 20) != height) {
        SetErrorLocked("PNG signature/IHDR sheet dimensions mismatch metadata");
        return InstalledAssetResult::kMetadataMismatch;
      }
      resource->metadata.frame_width = static_cast<std::uint32_t>(width);
      resource->metadata.frame_height = static_cast<std::uint32_t>(height);
      resource->metadata.frame_count = static_cast<std::uint32_t>(frames);
      resource->metadata.atlas = atlas;
    } else {
      const auto minimum_size = values.find("minimum_expected_size");
      const auto line_height = values.find("minimum_expected_line_height");
      const auto shading = values.find("allow_shading");
      std::uint64_t minimum = 0;
      std::uint64_t line = 0;
      if (kind != "Font" || minimum_size == values.end() ||
          line_height == values.end() || shading == values.end() ||
          !ParseUnsigned(minimum_size->second, &minimum) ||
          !ParseUnsigned(line_height->second, &line) ||
          minimum != spec.minimum_font_size ||
          line != spec.minimum_line_height || shading->second != "false" ||
          resource->data.size() != spec.exact_font_bytes) {
        SetErrorLocked("font metadata/size does not match audited manifest");
        return InstalledAssetResult::kMetadataMismatch;
      }
      if (resource->data.size() < 4 ||
          !(Big32(resource->data.data()) == 0x00010000U ||
            std::memcmp(resource->data.data(), "OTTO", 4) == 0 ||
            std::memcmp(resource->data.data(), "true", 4) == 0 ||
            std::memcmp(resource->data.data(), "ttcf", 4) == 0)) {
        SetErrorLocked("installed font has an unsupported sfnt signature");
        return InstalledAssetResult::kMetadataMismatch;
      }
      resource->metadata.minimum_font_size =
          static_cast<std::uint32_t>(minimum);
      resource->metadata.minimum_line_height =
          static_cast<std::uint32_t>(line);
      resource->metadata.allow_shading = false;
    }
    return InstalledAssetResult::kOk;
  }

  mutable std::mutex mutex_;
  InstalledAssetProviderConfig config_;
  HANDLE archive_ = INVALID_HANDLE_VALUE;
  std::uint64_t archive_size_ = 0;
  bool open_ = false;
  std::vector<AssetSpec> specs_;
  std::unordered_map<std::string, Entry> entries_;
  std::array<std::unordered_map<std::string, std::string>,
             kDynamicClassCount> dynamic_paths_;
  std::unordered_map<std::uint16_t, CacheEntry> cache_;
  std::size_t cache_bytes_ = 0;
  std::uint64_t cache_clock_ = 0;
  InstalledAssetCacheStats stats_;
  std::string last_error_;
};

InstalledAssetResult InstalledAssetProvider::Impl::LoadItemIcon(
    const InstalledItemIconRef& source,
    std::shared_ptr<const InstalledAssetResource>* output) {
  return LoadDynamicAnimation(
      {InstalledDynamicAssetClass::kItemIcon, source.logical_id}, output);
}

InstalledAssetResult InstalledAssetProvider::Impl::LoadDynamicAnimation(
    const InstalledDynamicAnimationRef& source,
    std::shared_ptr<const InstalledAssetResource>* output) {
  if (!output) return InstalledAssetResult::kIoError;
  output->reset();
  std::lock_guard<std::mutex> guard(mutex_);
  if (!open_) {
    SetErrorLocked("asset provider is not open");
    return InstalledAssetResult::kNotOpen;
  }
  const std::size_t class_index = DynamicClassIndex(source.asset_class);
  constexpr std::string_view suffix = ".meta.toml";
  if (class_index >= dynamic_paths_.size() ||
      !ExactLogicalAnimationId(source.logical_id)) {
    ++stats_.dynamic_failures;
    SetErrorLocked("dynamic animation class/logical ID is invalid");
    return InstalledAssetResult::kNotWhitelisted;
  }
  const auto resolved = dynamic_paths_[class_index].find(source.logical_id);
  if (resolved == dynamic_paths_[class_index].end() ||
      resolved->second.empty()) {
    ++stats_.dynamic_failures;
    SetErrorLocked("logical animation is absent or ambiguous in asset class");
    return InstalledAssetResult::kNotWhitelisted;
  }
  const std::string& metadata_path = resolved->second;
  const std::string data_path = metadata_path.substr(
      0, metadata_path.size() - suffix.size()) + ".png";
  const auto data_entry = entries_.find(data_path);
  const auto meta_entry = entries_.find(metadata_path);
  if (data_entry == entries_.end() || meta_entry == entries_.end()) {
    ++stats_.dynamic_failures;
    SetErrorLocked("resolved dynamic animation pair is missing");
    return InstalledAssetResult::kMissingPair;
  }
  std::vector<std::uint8_t> data;
  std::vector<std::uint8_t> metadata_bytes;
  InstalledAssetResult result = ReadEntry(data_entry->second,
                                          config_.max_data_bytes, &data);
  if (result != InstalledAssetResult::kOk) {
    ++stats_.dynamic_failures;
    return result;
  }
  result = ReadEntry(meta_entry->second, config_.max_metadata_bytes,
                     &metadata_bytes);
  if (result != InstalledAssetResult::kOk) {
    ++stats_.dynamic_failures;
    return result;
  }
  auto resource = std::make_shared<InstalledAssetResource>();
  resource->data = std::move(data);
  resource->metadata_text.assign(
      reinterpret_cast<const char*>(metadata_bytes.data()),
      metadata_bytes.size());
  resource->source_metadata_path = metadata_path;
  result = ValidateDynamicAnimation(source, resource.get());
  if (result != InstalledAssetResult::kOk) {
    ++stats_.dynamic_failures;
    return result;
  }
  ++stats_.dynamic_loads;
  *output = std::move(resource);
  last_error_.clear();
  return InstalledAssetResult::kOk;
}

InstalledAssetProvider::InstalledAssetProvider(
    InstalledAssetProviderConfig config)
    : impl_(std::make_unique<Impl>(config)) {}

InstalledAssetProvider::~InstalledAssetProvider() = default;

bool InstalledAssetProvider::OpenAdjacentToModule(HMODULE module) {
  return impl_->OpenModule(module);
}

void InstalledAssetProvider::Close() { impl_->Close(); }

bool InstalledAssetProvider::IsOpen() const { return impl_->IsOpen(); }

InstalledAssetResult InstalledAssetProvider::Load(
    InstalledAssetKey key,
    std::shared_ptr<const InstalledAssetResource>* output) {
  return impl_->Load(key, output);
}

InstalledAssetResult InstalledAssetProvider::LoadItemIcon(
    const InstalledItemIconRef& source,
    std::shared_ptr<const InstalledAssetResource>* output) {
  return impl_->LoadItemIcon(source, output);
}

InstalledAssetResult InstalledAssetProvider::LoadDynamicAnimation(
    const InstalledDynamicAnimationRef& source,
    std::shared_ptr<const InstalledAssetResource>* output) {
  return impl_->LoadDynamicAnimation(source, output);
}

InstalledAssetCacheStats InstalledAssetProvider::CacheStats() const {
  return impl_->Stats();
}

std::string InstalledAssetProvider::LastError() const { return impl_->Error(); }

#ifdef FOMDS_ASSET_PROVIDER_TESTING
bool InstalledAssetProvider::OpenSyntheticForTests(
    const std::wstring& game_directory,
    const SyntheticTrustProfile& trust,
    const std::vector<SyntheticAssetSpec>& whitelist) {
  return impl_->OpenSynthetic(game_directory, trust, whitelist);
}

bool InstalledAssetProvider::OpenExactDirectoryForTests(
    const std::wstring& game_directory) {
  return impl_->OpenExactDirectory(game_directory);
}
#endif

}  // namespace fomds

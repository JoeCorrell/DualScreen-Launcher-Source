#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fomds {

enum class InstalledAssetKey : std::uint16_t {
  kJournalBackplate,
  kInventoryLeftPage,
  kGenericBox,
  kGenericButton,
  kFontLut,
  kHealthBackplate,
  kHealthFill,
  kStaminaFill,
  kManaOrbOn,
  kInfoHudTop,
  kSunnyWeather,
  kSpringSeason,
  kCurrency,
  kEssence,
  kStatusEffectBarBack,
  kMapGrid,
  kBeachMap,
  kDeepWoodsMap,
  kDragonswornGladeMap,
  kEasternRoadMap,
  kFarmMap,
  kFarmExpansionMap,
  kHaydensFarmMap,
  kNarrowsMap,
  kSummitMap,
  kTownMap,
  kWesternRuinsMap,
  kPlayerMapMarker,
  kFarmMapTab,
  kRelationshipHeader,
  kRelationshipHearts,
  kAdelineSpringNeutral,
  kTurnipIcon,
  kAlmanacHeader,
  kAlmanacCrops,
  kFarmingSkill,
  kGreenSkillProgress,
  kMagicHeader,
  kMagicCard,
  kRestoreCardIcon,
  kAnimalHeader,
  kAnimalHearts,
  kAnimalPreviewBox,
  kChickenFemaleIcon,
  kMuseumOn,
  kRenown,
  kBirdseedFont,
  kPixelMPlusFont,
  kCubic11Font,
  kZpixFont,
  kSilverFont,
  kNosutaruFont,
};

enum class InstalledAssetKind : std::uint8_t { kAnimation, kFont };

struct InstalledAssetMetadata {
  InstalledAssetKind kind = InstalledAssetKind::kAnimation;
  std::string asset_id;
  std::uint32_t frame_width = 0;
  std::uint32_t frame_height = 0;
  std::uint32_t frame_count = 0;
  std::string atlas;
  std::uint32_t minimum_font_size = 0;
  std::uint32_t minimum_line_height = 0;
  bool allow_shading = false;
};

struct InstalledAssetResource {
  InstalledAssetKey key = InstalledAssetKey::kJournalBackplate;
  InstalledAssetMetadata metadata;
  std::vector<std::uint8_t> data;
  std::string metadata_text;
  // Non-empty only for a bridge-selected dynamic item icon.
  std::string source_metadata_path;
};

enum class InstalledDynamicAssetClass : std::uint8_t {
  kItemIcon,
  kNpcPortrait,
  kNpcMapIcon,
  kAnimalIcon,
  kSkillIcon,
  kSpellIcon,
};

// asset_to_string() supplies this already-resolved logical animation name.
// The class is selected by the typed bridge topic, never inferred from text.
struct InstalledDynamicAnimationRef {
  InstalledDynamicAssetClass asset_class =
      InstalledDynamicAssetClass::kItemIcon;
  std::string logical_id;
};

struct InstalledItemIconRef {
  std::string logical_id;
};

struct InstalledAssetProviderConfig {
  std::size_t max_cached_items = 16;
  std::size_t max_cached_bytes = 16 * 1024 * 1024;
  std::size_t max_data_bytes = 8 * 1024 * 1024;
  std::size_t max_metadata_bytes = 64 * 1024;
  std::uint64_t max_central_directory_bytes = 64 * 1024 * 1024;
  std::uint64_t max_archive_entries = 130000;
  std::size_t max_archive_name_bytes = 1024;
  std::uint64_t max_dynamic_image_pixels = 16ULL * 1024ULL * 1024ULL;
  std::uint32_t max_dynamic_image_dimension = 16384;
};

struct InstalledAssetCacheStats {
  std::size_t items = 0;
  std::size_t bytes = 0;
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;
  std::uint64_t evictions = 0;
  std::uint64_t dynamic_loads = 0;
  std::uint64_t dynamic_failures = 0;
};

enum class InstalledAssetResult : std::uint8_t {
  kOk,
  kNotOpen,
  kNotWhitelisted,
  kMissingPair,
  kUnsupportedEntry,
  kSizeLimit,
  kCorruptEntry,
  kMetadataMismatch,
  kCacheFull,
  kIoError,
};

class InstalledAssetProvider {
 public:
  explicit InstalledAssetProvider(InstalledAssetProviderConfig config = {});
  ~InstalledAssetProvider();
  InstalledAssetProvider(const InstalledAssetProvider&) = delete;
  InstalledAssetProvider& operator=(const InstalledAssetProvider&) = delete;

  // Resolves the game directory from the loaded mod DLL and never consults the
  // process current directory.
  bool OpenAdjacentToModule(HMODULE module);
  void Close();
  bool IsOpen() const;

  InstalledAssetResult Load(
      InstalledAssetKey key,
      std::shared_ptr<const InstalledAssetResource>* output);
  // Loads only an exact PNG/meta pair beneath the audited Item Icons root.
  // It never searches by asset ID, lists entries, or exposes arbitrary paths.
  InstalledAssetResult LoadItemIcon(
      const InstalledItemIconRef& source,
      std::shared_ptr<const InstalledAssetResource>* output);
  InstalledAssetResult LoadDynamicAnimation(
      const InstalledDynamicAnimationRef& source,
      std::shared_ptr<const InstalledAssetResource>* output);
  InstalledAssetCacheStats CacheStats() const;
  std::string LastError() const;

#ifdef FOMDS_ASSET_PROVIDER_TESTING
  struct SyntheticAssetSpec {
    InstalledAssetKey key = InstalledAssetKey::kJournalBackplate;
    InstalledAssetKind kind = InstalledAssetKind::kAnimation;
    std::string data_path;
    std::string metadata_path;
    std::string asset_id;
    std::uint32_t frame_width = 0;
    std::uint32_t frame_height = 0;
    std::uint32_t frame_count = 0;
    std::string atlas;
    std::uint32_t font_bytes = 0;
    std::uint32_t minimum_font_size = 0;
    std::uint32_t minimum_line_height = 0;
  };

  struct SyntheticTrustProfile {
    std::string original_sha256;
    std::uint64_t original_size = 0;
    std::uint64_t original_entries = 0;
    std::string patched_sha256;
    std::uint64_t patched_size = 0;
    std::uint64_t patched_entries = 0;
    std::string patch_manifest_sha256;
    std::string patched_game_sha256;
    std::string bridge_sha256;
  };

  bool OpenSyntheticForTests(
      const std::wstring& game_directory,
      const SyntheticTrustProfile& trust,
      const std::vector<SyntheticAssetSpec>& whitelist);
  bool OpenExactDirectoryForTests(const std::wstring& game_directory);
#endif

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fomds

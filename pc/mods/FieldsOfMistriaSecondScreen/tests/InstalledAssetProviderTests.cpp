#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

#include "BuildIdentity.h"
#include "InstalledAssetProvider.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::fprintf(stderr, "InstalledAssetProviderTests failed: %s (error=%lu)\n",
                 message.c_str(), static_cast<unsigned long>(GetLastError()));
    std::exit(1);
  }
}

class TestDirectory {
 public:
  TestDirectory() {
    wchar_t temporary[MAX_PATH] = {};
    Require(GetTempPathW(MAX_PATH, temporary) != 0, "GetTempPathW");
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
      wchar_t leaf[96] = {};
      std::swprintf(leaf, sizeof(leaf) / sizeof(leaf[0]),
                    L"fomds-assets-%lu-%llu-%u",
                    static_cast<unsigned long>(GetCurrentProcessId()),
                    static_cast<unsigned long long>(GetTickCount64()), attempt);
      path_ = temporary;
      path_.append(leaf);
      if (CreateDirectoryW(path_.c_str(), nullptr)) return;
      Require(GetLastError() == ERROR_ALREADY_EXISTS,
              "unexpected CreateDirectoryW error");
    }
    Require(false, "could not create unique test directory");
  }

  ~TestDirectory() {
    WIN32_FIND_DATAW entry = {};
    const std::wstring pattern = path_ + L"\\*";
    HANDLE find = FindFirstFileW(pattern.c_str(), &entry);
    if (find != INVALID_HANDLE_VALUE) {
      do {
        if (std::wcscmp(entry.cFileName, L".") == 0 ||
            std::wcscmp(entry.cFileName, L"..") == 0) continue;
        const std::wstring child = path_ + L"\\" + entry.cFileName;
        DeleteFileW(child.c_str());
      } while (FindNextFileW(find, &entry));
      FindClose(find);
    }
    RemoveDirectoryW(path_.c_str());
  }

  const std::wstring& path() const { return path_; }
  std::wstring file(const wchar_t* name) const {
    return path_ + L"\\" + name;
  }

 private:
  std::wstring path_;
};

void Put16(std::vector<std::uint8_t>* output, std::uint16_t value) {
  output->push_back(static_cast<std::uint8_t>(value));
  output->push_back(static_cast<std::uint8_t>(value >> 8U));
}

void Put32(std::vector<std::uint8_t>* output, std::uint32_t value) {
  Put16(output, static_cast<std::uint16_t>(value));
  Put16(output, static_cast<std::uint16_t>(value >> 16U));
}

void Put64(std::vector<std::uint8_t>* output, std::uint64_t value) {
  Put32(output, static_cast<std::uint32_t>(value));
  Put32(output, static_cast<std::uint32_t>(value >> 32U));
}

std::uint32_t Crc32(const std::vector<std::uint8_t>& data) {
  std::uint32_t crc = 0xffffffffU;
  for (std::uint8_t value : data) {
    crc ^= value;
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xedb88320U &
          static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1U)));
    }
  }
  return ~crc;
}

struct FixtureEntry {
  std::string name;
  std::vector<std::uint8_t> data;
  std::uint16_t method = 0;
  bool corrupt_crc = false;
};

void WriteBytes(const std::wstring& path, const std::vector<std::uint8_t>& data) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  Require(file != INVALID_HANDLE_VALUE, "create fixture file");
  DWORD written = 0;
  Require(data.size() <= MAXDWORD &&
              WriteFile(file, data.data(), static_cast<DWORD>(data.size()),
                        &written, nullptr) &&
              written == data.size() && FlushFileBuffers(file),
          "write fixture file");
  CloseHandle(file);
}

void WriteText(const std::wstring& path, const std::string& text) {
  WriteBytes(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

void WriteZip64(const std::wstring& path,
                const std::vector<FixtureEntry>& entries) {
  struct Central {
    FixtureEntry entry;
    std::uint32_t local_offset;
    std::uint32_t crc;
  };
  std::vector<std::uint8_t> zip;
  std::vector<Central> central;
  for (const FixtureEntry& entry : entries) {
    Require(zip.size() <= 0xffffffffULL && entry.name.size() <= 0xffffU &&
                entry.data.size() <= 0xffffffffULL,
            "fixture entry bounds");
    Central item{entry, static_cast<std::uint32_t>(zip.size()), Crc32(entry.data)};
    if (entry.corrupt_crc) item.crc ^= 0xffffffffU;
    Put32(&zip, 0x04034b50U);
    Put16(&zip, 45);
    Put16(&zip, 0);
    Put16(&zip, entry.method);
    Put16(&zip, 0);
    Put16(&zip, 0);
    Put32(&zip, item.crc);
    Put32(&zip, static_cast<std::uint32_t>(entry.data.size()));
    Put32(&zip, static_cast<std::uint32_t>(entry.data.size()));
    Put16(&zip, static_cast<std::uint16_t>(entry.name.size()));
    Put16(&zip, 0);
    zip.insert(zip.end(), entry.name.begin(), entry.name.end());
    zip.insert(zip.end(), entry.data.begin(), entry.data.end());
    central.push_back(std::move(item));
  }
  const std::uint64_t central_offset = zip.size();
  for (const Central& item : central) {
    Put32(&zip, 0x02014b50U);
    Put16(&zip, 45);
    Put16(&zip, 45);
    Put16(&zip, 0);
    Put16(&zip, item.entry.method);
    Put16(&zip, 0);
    Put16(&zip, 0);
    Put32(&zip, item.crc);
    Put32(&zip, static_cast<std::uint32_t>(item.entry.data.size()));
    Put32(&zip, static_cast<std::uint32_t>(item.entry.data.size()));
    Put16(&zip, static_cast<std::uint16_t>(item.entry.name.size()));
    Put16(&zip, 0);
    Put16(&zip, 0);
    Put16(&zip, 0);
    Put16(&zip, 0);
    Put32(&zip, 0);
    Put32(&zip, item.local_offset);
    zip.insert(zip.end(), item.entry.name.begin(), item.entry.name.end());
  }
  const std::uint64_t central_size = zip.size() - central_offset;
  const std::uint64_t zip64_offset = zip.size();
  Put32(&zip, 0x06064b50U);
  Put64(&zip, 44);
  Put16(&zip, 45);
  Put16(&zip, 45);
  Put32(&zip, 0);
  Put32(&zip, 0);
  Put64(&zip, entries.size());
  Put64(&zip, entries.size());
  Put64(&zip, central_size);
  Put64(&zip, central_offset);
  Put32(&zip, 0x07064b50U);
  Put32(&zip, 0);
  Put64(&zip, zip64_offset);
  Put32(&zip, 1);
  Put32(&zip, 0x06054b50U);
  Put16(&zip, 0);
  Put16(&zip, 0);
  Put16(&zip, 0xffffU);
  Put16(&zip, 0xffffU);
  Put32(&zip, 0xffffffffU);
  Put32(&zip, 0xffffffffU);
  Put16(&zip, 0);
  WriteBytes(path, zip);
}

std::vector<std::uint8_t> Png(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> png =
      {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
       0, 0, 0, 13, 'I', 'H', 'D', 'R'};
  const auto big = [&](std::uint32_t value) {
    png.push_back(static_cast<std::uint8_t>(value >> 24U));
    png.push_back(static_cast<std::uint8_t>(value >> 16U));
    png.push_back(static_cast<std::uint8_t>(value >> 8U));
    png.push_back(static_cast<std::uint8_t>(value));
  };
  big(width);
  big(height);
  png.insert(png.end(), {8, 6, 0, 0, 0, 0, 0, 0, 0});
  return png;
}

std::vector<std::uint8_t> Bytes(const std::string& value) {
  return {value.begin(), value.end()};
}

std::vector<FixtureEntry> GoodEntries() {
  return {
      {"assets/test/icon.png", Png(8, 2)},
      {"assets/test/icon.meta.toml", Bytes(
          "[meta_properties]\nid = \"1111222233334444\"\n"
          "asset_kind = \"Animation\"\n[asset_properties]\n"
          "frame_size = [4, 2]\nframe_len = 2\natlas = \"UI\"\n")},
      {"assets/fonts/test.ttf", {0, 1, 0, 0}},
      {"assets/fonts/test.meta.toml", Bytes(
          "[meta_properties]\nid = \"aaaabbbbccccdddd\"\n"
          "asset_kind = \"Font\"\n[asset_properties]\n"
          "minimum_expected_size = 12\n"
          "minimum_expected_line_height = 14\nallow_shading = false\n")},
  };
}

std::vector<FixtureEntry> EntriesWithDynamicItemIcon() {
  auto entries = GoodEntries();
  entries.push_back({
      "assets/animations/Item Icons/Test/spr_ui_test_icon.png", Png(8, 2)});
  entries.push_back({
      "assets/animations/Item Icons/Test/spr_ui_test_icon.meta.toml",
      Bytes("[meta_properties]\nid = \"0123456789abcdef\"\n"
            "asset_kind = \"Animation\"\n[asset_properties]\n"
            "frame_size = [4, 2]\nframe_len = 2\natlas = \"UI\"\n")});
  return entries;
}

std::vector<fomds::InstalledAssetProvider::SyntheticAssetSpec> Specs() {
  using K = fomds::InstalledAssetKey;
  using Kind = fomds::InstalledAssetKind;
  return {
      {K::kJournalBackplate, Kind::kAnimation, "assets/test/icon.png",
       "assets/test/icon.meta.toml", "1111222233334444", 4, 2, 2, "UI",
       0, 0, 0},
      {K::kBirdseedFont, Kind::kFont, "assets/fonts/test.ttf",
       "assets/fonts/test.meta.toml", "aaaabbbbccccdddd", 0, 0, 0, "",
       4, 12, 14},
  };
}

std::string Hash(const std::wstring& path) {
  const fomds::FileIdentity identity = fomds::IdentifyFile(path.c_str());
  Require(identity.present, "hash fixture");
  return identity.sha256;
}

std::uint64_t Size(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  Require(file != INVALID_HANDLE_VALUE, "open fixture for size");
  LARGE_INTEGER size = {};
  Require(GetFileSizeEx(file, &size) && size.QuadPart >= 0, "fixture size");
  CloseHandle(file);
  return static_cast<std::uint64_t>(size.QuadPart);
}

fomds::InstalledAssetProvider::SyntheticTrustProfile Trust(
    const std::wstring& original, std::uint64_t entries) {
  fomds::InstalledAssetProvider::SyntheticTrustProfile trust;
  trust.original_sha256 = Hash(original);
  trust.original_size = Size(original);
  trust.original_entries = entries;
  trust.patched_sha256 = std::string(64, 'a');
  trust.patched_size = 1;
  trust.patched_entries = entries + 1;
  trust.patch_manifest_sha256 = std::string(64, 'b');
  trust.patched_game_sha256 = std::string(64, 'c');
  trust.bridge_sha256 = std::string(64, 'd');
  return trust;
}

void TestFreshLoadCacheAndWhitelist() {
  TestDirectory directory;
  const std::wstring archive = directory.file(L"assets.zip");
  WriteZip64(archive, GoodEntries());
  fomds::InstalledAssetProviderConfig config;
  config.max_cached_items = 1;
  config.max_cached_bytes = 64 * 1024;
  fomds::InstalledAssetProvider provider(config);
  Require(provider.OpenSyntheticForTests(directory.path(), Trust(archive, 4),
                                         Specs()),
          "open exact synthetic ZIP64: " + provider.LastError());
  std::shared_ptr<const fomds::InstalledAssetResource> image;
  Require(provider.Load(fomds::InstalledAssetKey::kJournalBackplate, &image) ==
              fomds::InstalledAssetResult::kOk,
          "load validated raw PNG/meta pair");
  Require(image->metadata.frame_width == 4 &&
              image->metadata.frame_height == 2 &&
              image->metadata.frame_count == 2 &&
              image->metadata.asset_id == "1111222233334444",
          "parsed animation metadata");
  std::shared_ptr<const fomds::InstalledAssetResource> again;
  Require(provider.Load(fomds::InstalledAssetKey::kJournalBackplate, &again) ==
              fomds::InstalledAssetResult::kOk && image == again &&
              provider.CacheStats().hits == 1,
          "cache hit retains immutable resource");
  std::shared_ptr<const fomds::InstalledAssetResource> font;
  Require(provider.Load(fomds::InstalledAssetKey::kBirdseedFont, &font) ==
              fomds::InstalledAssetResult::kCacheFull,
          "retained cache entry prevents unsafe eviction");
  image.reset();
  again.reset();
  Require(provider.Load(fomds::InstalledAssetKey::kBirdseedFont, &font) ==
              fomds::InstalledAssetResult::kOk &&
              font->metadata.minimum_font_size == 12 &&
              provider.CacheStats().evictions == 1,
          "bounded LRU eviction and raw font load");
  Require(provider.Load(static_cast<fomds::InstalledAssetKey>(999), &again) ==
              fomds::InstalledAssetResult::kNotWhitelisted,
          "arbitrary asset key rejected");
}

void TestCorruptionMetadataAndUnsafeArchiveFailClosed() {
  {
    TestDirectory directory;
    auto entries = GoodEntries();
    entries[0].corrupt_crc = true;
    const std::wstring archive = directory.file(L"assets.zip");
    WriteZip64(archive, entries);
    fomds::InstalledAssetProvider provider;
    Require(provider.OpenSyntheticForTests(directory.path(), Trust(archive, 4),
                                           Specs()),
            "CRC fixture indexes");
    std::shared_ptr<const fomds::InstalledAssetResource> output;
    Require(provider.Load(fomds::InstalledAssetKey::kJournalBackplate, &output) ==
                fomds::InstalledAssetResult::kCorruptEntry,
            "CRC mismatch rejected");
  }
  {
    TestDirectory directory;
    auto entries = GoodEntries();
    entries[1].data = Bytes(
        "[meta_properties]\nid = \"wrong\"\nasset_kind = \"Animation\"\n"
        "[asset_properties]\nframe_size = [4, 2]\nframe_len = 2\n"
        "atlas = \"UI\"\n");
    const std::wstring archive = directory.file(L"assets.zip");
    WriteZip64(archive, entries);
    fomds::InstalledAssetProvider provider;
    Require(provider.OpenSyntheticForTests(directory.path(), Trust(archive, 4),
                                           Specs()),
            "bad metadata fixture indexes");
    std::shared_ptr<const fomds::InstalledAssetResource> output;
    Require(provider.Load(fomds::InstalledAssetKey::kJournalBackplate, &output) ==
                fomds::InstalledAssetResult::kMetadataMismatch,
            "paired metadata ID mismatch rejected");
  }
  {
    TestDirectory directory;
    auto entries = GoodEntries();
    entries.push_back({"../escape", {'x'}});
    const std::wstring archive = directory.file(L"assets.zip");
    WriteZip64(archive, entries);
    fomds::InstalledAssetProvider provider;
    Require(!provider.OpenSyntheticForTests(directory.path(), Trust(archive, 5),
                                            Specs()),
            "unsafe non-whitelist archive name rejects whole index");
  }
  {
    TestDirectory directory;
    auto entries = GoodEntries();
    entries[0].method = 8;
    const std::wstring archive = directory.file(L"assets.zip");
    WriteZip64(archive, entries);
    fomds::InstalledAssetProvider provider;
    Require(!provider.OpenSyntheticForTests(directory.path(), Trust(archive, 4),
                                            Specs()),
            "compressed whitelisted entry rejected");
  }
}

void TestBridgeSelectedDynamicItemIcon() {
  TestDirectory directory;
  const std::wstring archive = directory.file(L"assets.zip");
  WriteZip64(archive, EntriesWithDynamicItemIcon());
  fomds::InstalledAssetProvider provider;
  Require(provider.OpenSyntheticForTests(directory.path(), Trust(archive, 6),
                                         Specs()),
          "dynamic item fixture indexes");
  const fomds::InstalledItemIconRef source = {"spr_ui_test_icon"};
  std::shared_ptr<const fomds::InstalledAssetResource> resource;
  const fomds::InstalledAssetResult load_result =
      provider.LoadItemIcon(source, &resource);
  Require(load_result == fomds::InstalledAssetResult::kOk &&
              resource->metadata.asset_id == "0123456789abcdef" &&
              resource->metadata.frame_width == 4 &&
              resource->metadata.frame_height == 2 &&
              resource->metadata.frame_count == 2 &&
              resource->source_metadata_path ==
                  "assets/animations/Item Icons/Test/"
                  "spr_ui_test_icon.meta.toml",
          "exact bridge-selected item icon pair validates: " +
              std::to_string(static_cast<unsigned>(load_result)) + " " +
              provider.LastError());
  Require(provider.LoadDynamicAnimation(
              {fomds::InstalledDynamicAssetClass::kNpcMapIcon,
               source.logical_id},
              &resource) == fomds::InstalledAssetResult::kNotWhitelisted,
          "logical ID cannot cross its typed asset-class root");
  Require(provider.LoadItemIcon({"SPR_UI_TEST_ICON"}, &resource) ==
              fomds::InstalledAssetResult::kNotWhitelisted,
          "logical ID must use canonical lowercase engine form");
  Require(provider.LoadItemIcon({"spr_ui_missing_icon"}, &resource) ==
              fomds::InstalledAssetResult::kNotWhitelisted,
          "provider does not search by ID or substitute similarly named art");
  Require(provider.CacheStats().dynamic_loads == 1 &&
              provider.CacheStats().dynamic_failures == 3,
          "dynamic icon diagnostics are bounded counters, not enumeration");

  fomds::InstalledAssetProviderConfig bounded_config;
  bounded_config.max_dynamic_image_dimension = 4;
  fomds::InstalledAssetProvider bounded(bounded_config);
  Require(bounded.OpenSyntheticForTests(directory.path(), Trust(archive, 6),
                                        Specs()),
          "bounded dynamic provider opens fixture");
  Require(bounded.LoadItemIcon(source, &resource) ==
              fomds::InstalledAssetResult::kSizeLimit,
          "dynamic item sheet dimension cap is enforced");

  TestDirectory corrupt_directory;
  auto corrupt_entries = EntriesWithDynamicItemIcon();
  corrupt_entries[4].corrupt_crc = true;
  const std::wstring corrupt_archive = corrupt_directory.file(L"assets.zip");
  WriteZip64(corrupt_archive, corrupt_entries);
  fomds::InstalledAssetProvider corrupt;
  Require(corrupt.OpenSyntheticForTests(corrupt_directory.path(),
                                        Trust(corrupt_archive, 6), Specs()),
          "corrupt dynamic fixture indexes");
  Require(corrupt.LoadItemIcon(source, &resource) ==
              fomds::InstalledAssetResult::kCorruptEntry,
          "dynamic item PNG CRC mismatch fails closed");

  TestDirectory collision_directory;
  auto collision_entries = EntriesWithDynamicItemIcon();
  collision_entries.push_back({
      "assets/animations/Item Icons/Other/spr_ui_test_icon.png", Png(8, 2)});
  collision_entries.push_back({
      "assets/animations/Item Icons/Other/spr_ui_test_icon.meta.toml",
      Bytes("[meta_properties]\nid = \"fedcba9876543210\"\n"
            "asset_kind = \"Animation\"\n[asset_properties]\n"
            "frame_size = [4, 2]\nframe_len = 2\natlas = \"UI\"\n")});
  const std::wstring collision_archive =
      collision_directory.file(L"assets.zip");
  WriteZip64(collision_archive, collision_entries);
  fomds::InstalledAssetProvider collision;
  Require(collision.OpenSyntheticForTests(
              collision_directory.path(), Trust(collision_archive, 8), Specs()),
          "colliding logical-ID fixture indexes without enumeration");
  Require(collision.LoadItemIcon(source, &resource) ==
              fomds::InstalledAssetResult::kNotWhitelisted,
          "duplicate basename is excluded instead of guessed");
}

void TestOwnedTrustChain() {
  TestDirectory directory;
  const std::wstring backup = directory.file(
      L"assets.zip.fomds-original-e55ef6bcaa743000");
  const std::wstring live = directory.file(L"assets.zip");
  WriteZip64(backup, GoodEntries());
  auto patched_entries = GoodEntries();
  patched_entries.push_back({"assets/gml/scripts/TestBridge.gml", {'o', 'k'}});
  WriteZip64(live, patched_entries);
  auto trust = Trust(backup, 4);
  trust.patched_sha256 = Hash(live);
  trust.patched_size = Size(live);
  trust.patched_entries = 5;

  const std::string owner = "FieldsOfMistriaSecondScreen:assets:v1\n";
  const std::wstring owner_path = directory.file(
      L"FieldsOfMistriaSecondScreen.assets.owner");
  WriteText(owner_path, owner);
  const std::string owner_hash = Hash(owner_path);
  const std::string manifest =
      "{\"schema\":1,"
      "\"owner\":\"FieldsOfMistriaSecondScreen:assets:v1\","
      "\"targetExecutable\":\"FieldsOfMistria.exe\","
      "\"targetVersion\":\"1.0.4\","
      "\"targetExecutableSha256\":\"aed5b1f5d5cd75afee86f0cf347655a7e62be361d775d6ffc0671b388660ee2e\","
      "\"sourceArchiveName\":\"assets.zip\","
      "\"sourceArchiveSha256\":\"" + trust.original_sha256 + "\","
      "\"sourceArchiveSize\":" + std::to_string(trust.original_size) + ","
      "\"sourceArchiveEntries\":4,"
      "\"originalBackupName\":\"assets.zip.fomds-original-e55ef6bcaa743000\","
      "\"patchToolSchema\":1,"
      "\"patchManifestSha256\":\"" + trust.patch_manifest_sha256 + "\","
      "\"patchedArchiveSha256\":\"" + trust.patched_sha256 + "\","
      "\"patchedArchiveSize\":" + std::to_string(trust.patched_size) + ","
      "\"patchedArchiveEntries\":5,"
      "\"patchedGameSha256\":\"" + trust.patched_game_sha256 + "\","
      "\"bridgeSha256\":\"" + trust.bridge_sha256 + "\","
      "\"ownerSha256\":\"" + owner_hash + "\"}\n";
  const std::wstring manifest_path = directory.file(
      L"FieldsOfMistriaSecondScreen.assets.manifest.json");
  WriteText(manifest_path, manifest);

  fomds::InstalledAssetProvider provider;
  Require(provider.OpenSyntheticForTests(directory.path(), trust, Specs()),
          "owned patch/manifest/original-backup trust chain: " +
              provider.LastError());
  std::shared_ptr<const fomds::InstalledAssetResource> output;
  Require(provider.Load(fomds::InstalledAssetKey::kJournalBackplate, &output) ==
              fomds::InstalledAssetResult::kOk,
          "owned install reads verified permanent original backup");
  provider.Close();
  WriteText(owner_path, "tampered\n");
  Require(!provider.OpenSyntheticForTests(directory.path(), trust, Specs()),
          "tampered owner fails closed");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  TestFreshLoadCacheAndWhitelist();
  TestCorruptionMetadataAndUnsafeArchiveFailClosed();
  TestBridgeSelectedDynamicItemIcon();
  TestOwnedTrustChain();
  if (argc == 2) {
    fomds::InstalledAssetProvider provider;
    Require(provider.OpenExactDirectoryForTests(argv[1]),
            "real exact-v1.0.4 archive gate/index: " + provider.LastError());
    const auto last = static_cast<std::uint16_t>(
        fomds::InstalledAssetKey::kNosutaruFont);
    for (std::uint16_t value = 0; value <= last; ++value) {
      std::shared_ptr<const fomds::InstalledAssetResource> resource;
      Require(provider.Load(static_cast<fomds::InstalledAssetKey>(value),
                            &resource) == fomds::InstalledAssetResult::kOk,
              "real exact whitelist entry " + std::to_string(value) +
                  ": " + provider.LastError());
    }
    const std::vector<fomds::InstalledDynamicAnimationRef> dynamic_refs = {
        {fomds::InstalledDynamicAssetClass::kItemIcon,
         "spr_ui_item_turnip"},
        {fomds::InstalledDynamicAssetClass::kNpcPortrait,
         "spr_portrait_adeline_spring_neutral"},
        {fomds::InstalledDynamicAssetClass::kNpcMapIcon,
         "spr_ui_generic_icon_npc_small_player"},
        {fomds::InstalledDynamicAssetClass::kAnimalIcon,
         "spr_ui_icon_animal_chicken_basic_female"},
        {fomds::InstalledDynamicAssetClass::kSkillIcon,
         "spr_ui_skill_icon_farming"},
        {fomds::InstalledDynamicAssetClass::kSpellIcon,
         "spr_ui_journal_magic_restore_card_icon"},
    };
    for (const auto& source : dynamic_refs) {
      std::shared_ptr<const fomds::InstalledAssetResource> resource;
      Require(provider.LoadDynamicAnimation(source, &resource) ==
                  fomds::InstalledAssetResult::kOk && resource &&
                  !resource->metadata.asset_id.empty(),
              "exact typed logical animation resolver: " +
                  provider.LastError());
    }
  }
  std::puts("InstalledAssetProviderTests passed");
  return 0;
}

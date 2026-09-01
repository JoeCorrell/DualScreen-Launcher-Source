#include "InstalledAssetRenderer.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::fprintf(stderr, "InstalledAssetRendererTests failed: %s (error=%lu)\n",
                 message.c_str(), static_cast<unsigned long>(GetLastError()));
    std::exit(1);
  }
}

const std::vector<std::uint8_t>& TwoFramePng() {
  // Synthetic 2x1 RGBA PNG: opaque red, then opaque green.
  static const std::vector<std::uint8_t> bytes = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00,
      0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02,
      0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0xf4,
      0x22, 0x7f, 0x8a, 0x00, 0x00, 0x00, 0x0e, 0x49, 0x44, 0x41,
      0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x04,
      0x01, 0x10, 0xf8, 0x03, 0xfd, 0x53, 0xfd, 0x8f, 0x19, 0x00,
      0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
      0x82};
  return bytes;
}

std::shared_ptr<const fomds::InstalledAssetResource> Image(
    fomds::InstalledAssetKey key, std::uint32_t width = 1,
    std::uint32_t frames = 2) {
  auto resource = std::make_shared<fomds::InstalledAssetResource>();
  resource->key = key;
  resource->metadata.kind = fomds::InstalledAssetKind::kAnimation;
  resource->metadata.frame_width = width;
  resource->metadata.frame_height = 1;
  resource->metadata.frame_count = frames;
  resource->data = TwoFramePng();
  return resource;
}

std::shared_ptr<const fomds::InstalledAssetResource> InvalidFont() {
  auto resource = std::make_shared<fomds::InstalledAssetResource>();
  resource->key = fomds::InstalledAssetKey::kBirdseedFont;
  resource->metadata.kind = fomds::InstalledAssetKind::kFont;
  resource->data = {0x00, 0x01, 0x00, 0x00, 0x00};
  return resource;
}

class Dib {
 public:
  Dib(int width, int height) {
    dc_ = CreateCompatibleDC(nullptr);
    Require(dc_ != nullptr, "create memory DC");
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels_, nullptr, 0);
    Require(bitmap_ != nullptr && pixels_ != nullptr, "create 32-bit DIB");
    original_ = SelectObject(dc_, bitmap_);
    Require(original_ != nullptr && original_ != HGDI_ERROR, "select DIB");
  }

  ~Dib() {
    if (dc_ && original_) SelectObject(dc_, original_);
    if (bitmap_) DeleteObject(bitmap_);
    if (dc_) DeleteDC(dc_);
  }

  HDC dc() const { return dc_; }

 private:
  HDC dc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ original_ = nullptr;
  void* pixels_ = nullptr;
};

void TestInitializationDecodeDrawAndBounds() {
  fomds::InstalledAssetProvider provider;
  fomds::InstalledAssetRenderer renderer(provider);
  Require(renderer.PrefetchImage(fomds::InstalledAssetKey::kJournalBackplate) ==
              fomds::InstalledAssetRenderResult::kNotInitialized,
          "use before Initialize fails closed");
  Require(renderer.Initialize() == fomds::InstalledAssetRenderResult::kOk &&
              renderer.IsInitialized(),
          "GDI+ initializes outside DllMain");
  Require(renderer.InjectResourceForTests(
              Image(fomds::InstalledAssetKey::kJournalBackplate)) ==
              fomds::InstalledAssetRenderResult::kOk,
          "decode bounded synthetic PNG");

  Dib dib(20, 10);
  const RECT left = {0, 0, 10, 10};
  const RECT right = {10, 0, 20, 10};
  Require(renderer.DrawFrame(dib.dc(),
                             fomds::InstalledAssetKey::kJournalBackplate, 0,
                             left) == fomds::InstalledAssetRenderResult::kOk,
          "draw first horizontal-strip frame");
  Require(renderer.DrawFrame(dib.dc(),
                             fomds::InstalledAssetKey::kJournalBackplate, 1,
                             right) == fomds::InstalledAssetRenderResult::kOk,
          "draw second horizontal-strip frame");
  Require(GetPixel(dib.dc(), 5, 5) == RGB(255, 0, 0),
          "first frame raster is red");
  Require(GetPixel(dib.dc(), 15, 5) == RGB(0, 255, 0),
          "second frame raster is green");
  Require(renderer.DrawFrame(dib.dc(),
                             fomds::InstalledAssetKey::kJournalBackplate, 2,
                             left) ==
              fomds::InstalledAssetRenderResult::kFrameOutOfRange,
          "frame bounds enforced");
  Require(renderer.DrawFrame(nullptr,
                             fomds::InstalledAssetKey::kJournalBackplate, 0,
                             left) ==
              fomds::InstalledAssetRenderResult::kInvalidArgument,
          "invalid HDC rejected");

  const auto stats = renderer.Stats();
  Require(stats.cached_images == 1 && stats.cached_fonts == 0 &&
              stats.cached_bytes >= TwoFramePng().size(),
          "decoded cache accounting");
  renderer.Shutdown();
  Require(!renderer.IsInitialized() && renderer.Stats().cached_images == 0,
          "shutdown deterministically releases decoded cache");
}

void TestDecodeAndCacheFailures() {
  fomds::InstalledAssetProvider provider;
  fomds::InstalledAssetRendererConfig config;
  config.max_cached_items = 1;
  config.max_cached_bytes = 1024;
  fomds::InstalledAssetRenderer renderer(provider, config);
  Require(renderer.Initialize() == fomds::InstalledAssetRenderResult::kOk,
          "bounded renderer initializes");
  Require(renderer.InjectResourceForTests(
              Image(fomds::InstalledAssetKey::kJournalBackplate)) ==
              fomds::InstalledAssetRenderResult::kOk,
          "first cache entry");
  Require(renderer.InjectResourceForTests(
              Image(fomds::InstalledAssetKey::kInventoryLeftPage)) ==
              fomds::InstalledAssetRenderResult::kOk,
          "LRU cache accepts replacement");
  Require(renderer.Stats().cached_images == 1 &&
              renderer.Stats().evictions == 1,
          "LRU cache remains within item limit");
  Require(renderer.InjectResourceForTests(
              Image(fomds::InstalledAssetKey::kGenericBox, 1, 3)) ==
              fomds::InstalledAssetRenderResult::kDimensionMismatch,
          "decoded sheet mismatch rejected");
  Require(renderer.InjectResourceForTests(InvalidFont()) ==
              fomds::InstalledAssetRenderResult::kFontLoadFailure,
          "invalid in-memory font rejected");
  Require(renderer.Stats().decode_failures >= 2,
          "decode failures are diagnosed and counted");
}

void TestExactInstalledResources(const std::wstring& directory) {
  fomds::InstalledAssetProvider provider;
  Require(provider.OpenExactDirectoryForTests(directory),
          "open exact installed archive: " + provider.LastError());
  fomds::InstalledAssetRenderer renderer(provider);
  Require(renderer.Initialize() == fomds::InstalledAssetRenderResult::kOk,
          "exact renderer initializes");
  Require(renderer.PrefetchImage(
              fomds::InstalledAssetKey::kJournalBackplate) ==
              fomds::InstalledAssetRenderResult::kOk,
          "decode exact installed journal image: " + renderer.LastError());
  Require(renderer.PrefetchFont(fomds::InstalledAssetKey::kBirdseedFont) ==
              fomds::InstalledAssetRenderResult::kOk,
          "load exact installed Birdseed font from memory: " +
              renderer.LastError());

  static constexpr std::array<fomds::InstalledAssetKey, 11> kCanonicalMaps = {
      fomds::InstalledAssetKey::kBeachMap,
      fomds::InstalledAssetKey::kDeepWoodsMap,
      fomds::InstalledAssetKey::kDragonswornGladeMap,
      fomds::InstalledAssetKey::kEasternRoadMap,
      fomds::InstalledAssetKey::kFarmMap,
      fomds::InstalledAssetKey::kFarmExpansionMap,
      fomds::InstalledAssetKey::kHaydensFarmMap,
      fomds::InstalledAssetKey::kNarrowsMap,
      fomds::InstalledAssetKey::kSummitMap,
      fomds::InstalledAssetKey::kTownMap,
      fomds::InstalledAssetKey::kWesternRuinsMap,
  };
  for (const fomds::InstalledAssetKey map : kCanonicalMaps) {
    Require(renderer.PrefetchImage(map) ==
                fomds::InstalledAssetRenderResult::kOk,
            "decode canonical installed base map: " + renderer.LastError());
  }

  const auto first_image = static_cast<std::uint16_t>(
      fomds::InstalledAssetKey::kJournalBackplate);
  const auto last_image =
      static_cast<std::uint16_t>(fomds::InstalledAssetKey::kRenown);
  for (std::uint16_t value = first_image; value <= last_image; ++value) {
    Require(renderer.PrefetchImage(
                static_cast<fomds::InstalledAssetKey>(value)) ==
                fomds::InstalledAssetRenderResult::kOk,
            "decode every exact whitelisted animation under LRU pressure: " +
                renderer.LastError());
  }
  const auto first_font = static_cast<std::uint16_t>(
      fomds::InstalledAssetKey::kBirdseedFont);
  const auto last_font =
      static_cast<std::uint16_t>(fomds::InstalledAssetKey::kNosutaruFont);
  for (std::uint16_t value = first_font; value <= last_font; ++value) {
    Require(renderer.PrefetchFont(
                static_cast<fomds::InstalledAssetKey>(value)) ==
                fomds::InstalledAssetRenderResult::kOk,
            "load every exact whitelisted font under LRU pressure: " +
                renderer.LastError());
  }
  Require(renderer.Stats().evictions > 0,
          "full exact manifest exercises bounded decoded LRU eviction");
  const fomds::InstalledItemIconRef turnip = {"spr_ui_item_turnip"};
  Require(renderer.PrefetchItemIcon(turnip) ==
              fomds::InstalledAssetRenderResult::kOk,
          "decode bridge-selected exact installed item icon: " +
              renderer.LastError());
  const fomds::InstalledDynamicAnimationRef player_marker = {
      fomds::InstalledDynamicAssetClass::kNpcMapIcon,
      "spr_ui_generic_icon_npc_small_player"};
  Require(renderer.PrefetchDynamicImage(player_marker) ==
              fomds::InstalledAssetRenderResult::kOk,
          "decode typed bridge-selected NPC map icon: " +
              renderer.LastError());

  Dib dib(256, 128);
  const RECT image = {0, 0, 205, 117};
  Require(renderer.DrawFrame(dib.dc(),
                             fomds::InstalledAssetKey::kJournalBackplate, 0,
                             image) == fomds::InstalledAssetRenderResult::kOk,
          "draw exact installed journal image");
  for (const fomds::InstalledAssetKey map : kCanonicalMaps) {
    Require(renderer.DrawFrame(dib.dc(), map, 0, image) ==
                fomds::InstalledAssetRenderResult::kOk,
            "draw canonical installed base map: " + renderer.LastError());
  }
  const RECT icon = {220, 90, 256, 126};
  Require(renderer.DrawItemIconFrame(dib.dc(), turnip, 0, icon) ==
              fomds::InstalledAssetRenderResult::kOk,
          "draw bridge-selected exact installed item icon: " +
              renderer.LastError());
  Require(renderer.DrawDynamicFrame(dib.dc(), player_marker, 0, icon) ==
              fomds::InstalledAssetRenderResult::kOk,
          "draw typed bridge-selected NPC map icon: " +
              renderer.LastError());
  fomds::InstalledItemIconRef mismatched_turnip = turnip;
  mismatched_turnip.logical_id = "spr_ui_item_not_real";
  Require(renderer.PrefetchItemIcon(mismatched_turnip) ==
              fomds::InstalledAssetRenderResult::kProviderFailure,
          "renderer preserves provider logical-ID fail-closed gate");
  mismatched_turnip.logical_id.assign(4097, 'a');
  Require(renderer.PrefetchItemIcon(mismatched_turnip) ==
              fomds::InstalledAssetRenderResult::kInvalidArgument,
          "renderer rejects oversized logical IDs before cache-key allocation");
  fomds::InstalledAssetTextStyle style;
  style.pixel_size = 16.0F;
  SIZE measured = {};
  Require(renderer.MeasureText(dib.dc(),
                               fomds::InstalledAssetKey::kBirdseedFont,
                               L"FIELDS OF MISTRIA", style, &measured) ==
              fomds::InstalledAssetRenderResult::kOk &&
              measured.cx > 0 && measured.cy > 0,
          "measure exact installed private font: " + renderer.LastError());
  const RECT text = {4, 4, 252, 40};
  Require(renderer.DrawText(dib.dc(),
                            fomds::InstalledAssetKey::kBirdseedFont,
                            L"FIELDS OF MISTRIA", text, style) ==
              fomds::InstalledAssetRenderResult::kOk,
          "draw exact installed private font: " + renderer.LastError());
  Require(renderer.PrefetchImage(fomds::InstalledAssetKey::kBirdseedFont) ==
              fomds::InstalledAssetRenderResult::kWrongAssetKind,
          "font cannot cross the image API boundary");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  TestInitializationDecodeDrawAndBounds();
  TestDecodeAndCacheFailures();
  if (argc == 2) TestExactInstalledResources(argv[1]);
  std::puts("InstalledAssetRendererTests passed");
  return 0;
}

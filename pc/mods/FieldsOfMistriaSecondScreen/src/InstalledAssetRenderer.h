#pragma once

#include "InstalledAssetProvider.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace fomds {

enum class InstalledAssetRenderResult : std::uint8_t {
  kOk,
  kNotInitialized,
  kInvalidArgument,
  kProviderFailure,
  kWrongAssetKind,
  kDecodeFailure,
  kDimensionMismatch,
  kFrameOutOfRange,
  kFontLoadFailure,
  kCacheFull,
  kDrawFailure,
};

struct InstalledAssetRendererConfig {
  std::size_t max_cached_items = 16;
  std::size_t max_cached_bytes = 48 * 1024 * 1024;
  std::uint64_t max_image_pixels = 16ULL * 1024ULL * 1024ULL;
  std::uint32_t max_image_dimension = 16384;
  std::size_t max_text_characters = 4096;
  float minimum_font_pixels = 4.0F;
  float maximum_font_pixels = 160.0F;
};

struct InstalledAssetRendererStats {
  std::size_t cached_images = 0;
  std::size_t cached_fonts = 0;
  std::size_t cached_bytes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t cache_misses = 0;
  std::uint64_t evictions = 0;
  std::uint64_t decode_failures = 0;
  std::uint64_t draw_failures = 0;
};

struct InstalledAssetFrameStyle {
  std::uint8_t opacity = 255;
  bool nearest_neighbor = true;
};

enum class InstalledTextAlignment : std::uint8_t { kLeft, kCenter, kRight };

struct InstalledAssetTextStyle {
  float pixel_size = 16.0F;
  std::uint32_t argb = 0xff4b3427U;
  bool bold = false;
  bool italic = false;
  bool wrap = false;
  bool ellipsis = false;
  InstalledTextAlignment alignment = InstalledTextAlignment::kLeft;
};

// Decodes and rasterizes only resources returned by InstalledAssetProvider.
// The provider remains the sole authority for archive identity and the
// production whitelist. All methods are serialized and safe to call from a
// panel render thread; prefetch calls may be used before the first paint.
class InstalledAssetRenderer {
 public:
  explicit InstalledAssetRenderer(
      InstalledAssetProvider& provider,
      InstalledAssetRendererConfig config = {});
  ~InstalledAssetRenderer();
  InstalledAssetRenderer(const InstalledAssetRenderer&) = delete;
  InstalledAssetRenderer& operator=(const InstalledAssetRenderer&) = delete;

  // Must be called outside DllMain. Starts GDI+ with external codecs disabled.
  InstalledAssetRenderResult Initialize();
  void Shutdown();
  bool IsInitialized() const;

  InstalledAssetRenderResult PrefetchImage(InstalledAssetKey key);
  InstalledAssetRenderResult PrefetchFont(InstalledAssetKey key);
  InstalledAssetRenderResult PrefetchItemIcon(
      const InstalledItemIconRef& source);
  InstalledAssetRenderResult PrefetchDynamicImage(
      const InstalledDynamicAnimationRef& source);

  InstalledAssetRenderResult DrawFrame(
      HDC destination_dc, InstalledAssetKey key, std::uint32_t frame_index,
      const RECT& destination,
      const InstalledAssetFrameStyle& style = {});

  InstalledAssetRenderResult DrawItemIconFrame(
      HDC destination_dc, const InstalledItemIconRef& source,
      std::uint32_t frame_index, const RECT& destination,
      const InstalledAssetFrameStyle& style = {});
  InstalledAssetRenderResult DrawDynamicFrame(
      HDC destination_dc, const InstalledDynamicAnimationRef& source,
      std::uint32_t frame_index, const RECT& destination,
      const InstalledAssetFrameStyle& style = {});

  InstalledAssetRenderResult DrawText(
      HDC destination_dc, InstalledAssetKey font_key, std::wstring_view text,
      const RECT& layout, const InstalledAssetTextStyle& style = {});

  InstalledAssetRenderResult MeasureText(
      HDC reference_dc, InstalledAssetKey font_key, std::wstring_view text,
      const InstalledAssetTextStyle& style, SIZE* measured_pixels);

  InstalledAssetRendererStats Stats() const;
  std::string LastError() const;

#ifdef FOMDS_ASSET_RENDERER_TESTING
  // Test-only injection exercises the same bounded decoders without weakening
  // the production provider/path/whitelist boundary.
  InstalledAssetRenderResult InjectResourceForTests(
      std::shared_ptr<const InstalledAssetResource> resource);
#endif

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fomds

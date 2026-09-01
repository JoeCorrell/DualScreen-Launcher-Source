#include "InstalledAssetRenderer.h"

#include <gdiplus.h>
#include <objidl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <unordered_map>
#include <utility>

namespace fomds {
namespace {

std::string StaticCacheKey(InstalledAssetKey key) {
  return "static:" +
      std::to_string(static_cast<std::uint16_t>(key));
}

std::string DynamicCacheKey(const InstalledDynamicAnimationRef& source) {
  return "dynamic:" +
      std::to_string(static_cast<std::uint8_t>(source.asset_class)) + ":" +
      source.logical_id;
}

bool ValidDynamicReference(const InstalledDynamicAnimationRef& source) {
  return static_cast<std::uint8_t>(source.asset_class) < 6 &&
      source.logical_id.size() >= 4 && source.logical_id.size() <= 192 &&
      std::all_of(source.logical_id.begin(), source.logical_id.end(),
                  [](char character) {
                    return (character >= 'a' && character <= 'z') ||
                        (character >= '0' && character <= '9') ||
                        character == '_';
                  });
}

bool CheckedAdd(std::size_t left, std::size_t right, std::size_t* output) {
  if (!output || left > std::numeric_limits<std::size_t>::max() - right) {
    return false;
  }
  *output = left + right;
  return true;
}

int FontStyleValue(const InstalledAssetTextStyle& style) {
  int value = Gdiplus::FontStyleRegular;
  if (style.bold) value |= Gdiplus::FontStyleBold;
  if (style.italic) value |= Gdiplus::FontStyleItalic;
  return value;
}

}  // namespace

class InstalledAssetRenderer::Impl {
  struct ImageEntry;
  struct FontEntry;

 public:
  Impl(InstalledAssetProvider& provider, InstalledAssetRendererConfig config)
      : provider_(provider), config_(config) {
    if (config_.max_cached_items == 0) config_.max_cached_items = 1;
    if (config_.max_cached_bytes == 0) config_.max_cached_bytes = 1;
    if (config_.max_image_pixels == 0) config_.max_image_pixels = 1;
    if (config_.max_image_dimension == 0) config_.max_image_dimension = 1;
    if (config_.max_text_characters == 0) config_.max_text_characters = 1;
    if (!std::isfinite(config_.minimum_font_pixels) ||
        config_.minimum_font_pixels <= 0.0F) {
      config_.minimum_font_pixels = 4.0F;
    }
    if (!std::isfinite(config_.maximum_font_pixels) ||
        config_.maximum_font_pixels < config_.minimum_font_pixels) {
      config_.maximum_font_pixels = config_.minimum_font_pixels;
    }
  }

  ~Impl() { Shutdown(); }

  InstalledAssetRenderResult Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return InstalledAssetRenderResult::kOk;
    Gdiplus::GdiplusStartupInput input;
    input.SuppressExternalCodecs = TRUE;
    ULONG_PTR token = 0;
    const Gdiplus::Status status =
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
    if (status != Gdiplus::Ok || token == 0) {
      SetErrorLocked("GDI+ startup failed", status);
      return InstalledAssetRenderResult::kNotInitialized;
    }
    gdiplus_token_ = token;
    initialized_ = true;
    return InstalledAssetRenderResult::kOk;
  }

  void Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    images_.clear();
    fonts_.clear();
    stats_.cached_images = 0;
    stats_.cached_fonts = 0;
    stats_.cached_bytes = 0;
    if (gdiplus_token_ != 0) {
      Gdiplus::GdiplusShutdown(gdiplus_token_);
      gdiplus_token_ = 0;
    }
    initialized_ = false;
  }

  bool IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
  }

  InstalledAssetRenderResult Prefetch(InstalledAssetKey key,
                                      InstalledAssetKind expected_kind) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) return NotInitializedLocked();
      if (expected_kind == InstalledAssetKind::kAnimation) {
        ImageEntry* unused = nullptr;
        return AcquireImageLocked(key, &unused);
      }
      FontEntry* unused = nullptr;
      return AcquireFontLocked(key, &unused);
    } catch (const std::bad_alloc&) {
      return AllocationFailure();
    } catch (...) {
      return UnexpectedFailure();
    }
  }

  InstalledAssetRenderResult PrefetchItemIcon(
      const InstalledItemIconRef& source) {
    return PrefetchDynamicImage(
        {InstalledDynamicAssetClass::kItemIcon, source.logical_id});
  }

  InstalledAssetRenderResult PrefetchDynamicImage(
      const InstalledDynamicAnimationRef& source) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) return NotInitializedLocked();
      ImageEntry* unused = nullptr;
      return AcquireDynamicImageLocked(source, &unused);
    } catch (const std::bad_alloc&) {
      return AllocationFailure();
    } catch (...) {
      return UnexpectedFailure();
    }
  }

  InstalledAssetRenderResult DrawFrame(
      HDC destination_dc, InstalledAssetKey key, std::uint32_t frame_index,
      const RECT& destination, const InstalledAssetFrameStyle& style) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) return NotInitializedLocked();
      if (!destination_dc || destination.right <= destination.left ||
          destination.bottom <= destination.top) {
        SetErrorLocked("DrawFrame received an invalid HDC or rectangle");
        return InstalledAssetRenderResult::kInvalidArgument;
      }
      ImageEntry* entry = nullptr;
      InstalledAssetRenderResult result = AcquireImageLocked(key, &entry);
      if (result != InstalledAssetRenderResult::kOk) return result;
      return DrawLoadedFrameLocked(destination_dc, *entry, frame_index,
                                   destination, style);
    } catch (const std::bad_alloc&) {
      return AllocationFailure();
    } catch (...) {
      return UnexpectedFailure();
    }
  }

  InstalledAssetRenderResult DrawItemIconFrame(
      HDC destination_dc, const InstalledItemIconRef& source,
      std::uint32_t frame_index, const RECT& destination,
      const InstalledAssetFrameStyle& style) {
    return DrawDynamicFrame(
        destination_dc,
        {InstalledDynamicAssetClass::kItemIcon, source.logical_id},
        frame_index, destination, style);
  }

  InstalledAssetRenderResult DrawDynamicFrame(
      HDC destination_dc, const InstalledDynamicAnimationRef& source,
      std::uint32_t frame_index, const RECT& destination,
      const InstalledAssetFrameStyle& style) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) return NotInitializedLocked();
      if (!destination_dc || destination.right <= destination.left ||
          destination.bottom <= destination.top) {
        SetErrorLocked("DrawItemIconFrame received an invalid HDC or rectangle");
        return InstalledAssetRenderResult::kInvalidArgument;
      }
      ImageEntry* entry = nullptr;
      InstalledAssetRenderResult result =
          AcquireDynamicImageLocked(source, &entry);
      if (result != InstalledAssetRenderResult::kOk) return result;
      return DrawLoadedFrameLocked(destination_dc, *entry, frame_index,
                                   destination, style);
    } catch (const std::bad_alloc&) {
      return AllocationFailure();
    } catch (...) {
      return UnexpectedFailure();
    }
  }

  InstalledAssetRenderResult DrawLoadedFrameLocked(
      HDC destination_dc, ImageEntry& entry, std::uint32_t frame_index,
      const RECT& destination, const InstalledAssetFrameStyle& style) {
      if (frame_index >= entry.frame_count) {
        SetErrorLocked("DrawFrame frame index is outside the audited sheet");
        return InstalledAssetRenderResult::kFrameOutOfRange;
      }

      Gdiplus::Graphics graphics(destination_dc);
      if (graphics.GetLastStatus() != Gdiplus::Ok) {
        return DrawFailureLocked("GDI+ could not bind the destination HDC",
                                 graphics.GetLastStatus());
      }
      graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
      graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
      graphics.SetPixelOffsetMode(style.nearest_neighbor
                                      ? Gdiplus::PixelOffsetModeHalf
                                      : Gdiplus::PixelOffsetModeDefault);
      graphics.SetInterpolationMode(
          style.nearest_neighbor ? Gdiplus::InterpolationModeNearestNeighbor
                                 : Gdiplus::InterpolationModeHighQualityBicubic);

      Gdiplus::ImageAttributes attributes;
      Gdiplus::ImageAttributes* attributes_ptr = nullptr;
      if (style.opacity != 255) {
        const Gdiplus::REAL alpha =
            static_cast<Gdiplus::REAL>(style.opacity) / 255.0F;
        Gdiplus::ColorMatrix matrix = {{{1.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                                        {0.0F, 1.0F, 0.0F, 0.0F, 0.0F},
                                        {0.0F, 0.0F, 1.0F, 0.0F, 0.0F},
                                        {0.0F, 0.0F, 0.0F, alpha, 0.0F},
                                        {0.0F, 0.0F, 0.0F, 0.0F, 1.0F}}};
        if (attributes.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault,
                                      Gdiplus::ColorAdjustTypeBitmap) !=
            Gdiplus::Ok) {
          return DrawFailureLocked("GDI+ rejected the opacity matrix");
        }
        attributes_ptr = &attributes;
      }

      const INT source_x = static_cast<INT>(
          frame_index * static_cast<std::uint64_t>(entry.frame_width));
      const Gdiplus::Status status = graphics.DrawImage(
          entry.bitmap.get(),
          Gdiplus::Rect(destination.left, destination.top,
                        destination.right - destination.left,
                        destination.bottom - destination.top),
          source_x, 0, static_cast<INT>(entry.frame_width),
          static_cast<INT>(entry.frame_height), Gdiplus::UnitPixel,
          attributes_ptr);
      if (status != Gdiplus::Ok) {
        return DrawFailureLocked("GDI+ DrawImage failed", status);
      }
      return InstalledAssetRenderResult::kOk;
  }

  InstalledAssetRenderResult DrawText(
      HDC destination_dc, InstalledAssetKey font_key, std::wstring_view text,
      const RECT& layout, const InstalledAssetTextStyle& style) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) return NotInitializedLocked();
      if (!destination_dc || layout.right <= layout.left ||
          layout.bottom <= layout.top || !ValidTextLocked(text, style)) {
        SetErrorLocked("DrawText received invalid arguments");
        return InstalledAssetRenderResult::kInvalidArgument;
      }
      FontEntry* entry = nullptr;
      InstalledAssetRenderResult result = AcquireFontLocked(font_key, &entry);
      if (result != InstalledAssetRenderResult::kOk) return result;
      return DrawOrMeasureTextLocked(destination_dc, *entry, text, style,
                                     &layout, nullptr);
    } catch (const std::bad_alloc&) {
      return AllocationFailure();
    } catch (...) {
      return UnexpectedFailure();
    }
  }

  InstalledAssetRenderResult MeasureText(
      HDC reference_dc, InstalledAssetKey font_key, std::wstring_view text,
      const InstalledAssetTextStyle& style, SIZE* measured_pixels) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) return NotInitializedLocked();
      if (!reference_dc || !measured_pixels ||
          !ValidTextLocked(text, style)) {
        SetErrorLocked("MeasureText received invalid arguments");
        return InstalledAssetRenderResult::kInvalidArgument;
      }
      measured_pixels->cx = 0;
      measured_pixels->cy = 0;
      FontEntry* entry = nullptr;
      InstalledAssetRenderResult result = AcquireFontLocked(font_key, &entry);
      if (result != InstalledAssetRenderResult::kOk) return result;
      return DrawOrMeasureTextLocked(reference_dc, *entry, text, style,
                                     nullptr, measured_pixels);
    } catch (const std::bad_alloc&) {
      return AllocationFailure();
    } catch (...) {
      return UnexpectedFailure();
    }
  }

  InstalledAssetRendererStats Stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
  }

  std::string LastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
  }

#ifdef FOMDS_ASSET_RENDERER_TESTING
  InstalledAssetRenderResult InjectResourceForTests(
      std::shared_ptr<const InstalledAssetResource> resource) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) return NotInitializedLocked();
      if (!resource) {
        SetErrorLocked("test resource is null");
        return InstalledAssetRenderResult::kInvalidArgument;
      }
      const std::string cache_key = StaticCacheKey(resource->key);
      if (resource->metadata.kind == InstalledAssetKind::kAnimation) {
        return DecodeAndCacheImageLocked(std::move(resource), cache_key,
                                         nullptr);
      }
      return DecodeAndCacheFontLocked(std::move(resource), cache_key, nullptr);
    } catch (const std::bad_alloc&) {
      return AllocationFailure();
    } catch (...) {
      return UnexpectedFailure();
    }
  }
#endif

 private:
  struct ImageEntry {
    IStream* stream = nullptr;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    std::uint32_t frame_width = 0;
    std::uint32_t frame_height = 0;
    std::uint32_t frame_count = 0;
    std::size_t charge = 0;
    std::uint64_t last_use = 0;

    ~ImageEntry() {
      bitmap.reset();
      if (stream) stream->Release();
    }
  };

  struct FontEntry {
    // GDI+ requires AddMemoryFont input to remain valid for the collection's
    // lifetime. Keep a renderer-owned copy so the provider's smaller raw LRU
    // is never pinned by decoded entries.
    std::vector<std::uint8_t> bytes;
    std::unique_ptr<Gdiplus::PrivateFontCollection> collection;
    std::unique_ptr<Gdiplus::FontFamily> family;
    std::size_t charge = 0;
    std::uint64_t last_use = 0;
  };

  InstalledAssetRenderResult AcquireImageLocked(InstalledAssetKey key,
                                                 ImageEntry** output) {
    const std::string cache_key = StaticCacheKey(key);
    auto found = images_.find(cache_key);
    if (found != images_.end()) {
      ++stats_.cache_hits;
      found->second->last_use = NextUseLocked();
      if (output) *output = found->second.get();
      return InstalledAssetRenderResult::kOk;
    }
    ++stats_.cache_misses;
    std::shared_ptr<const InstalledAssetResource> resource;
    const InstalledAssetResult provider_result = provider_.Load(key, &resource);
    if (provider_result != InstalledAssetResult::kOk || !resource) {
      SetErrorLocked("installed asset provider rejected image request",
                     static_cast<long>(provider_result));
      return InstalledAssetRenderResult::kProviderFailure;
    }
    return DecodeAndCacheImageLocked(std::move(resource), cache_key, output);
  }

  InstalledAssetRenderResult AcquireDynamicImageLocked(
      const InstalledDynamicAnimationRef& source, ImageEntry** output) {
    if (!ValidDynamicReference(source)) {
      SetErrorLocked("dynamic image class/logical ID is invalid");
      return InstalledAssetRenderResult::kInvalidArgument;
    }
    const std::string cache_key = DynamicCacheKey(source);
    auto found = images_.find(cache_key);
    if (found != images_.end()) {
      ++stats_.cache_hits;
      found->second->last_use = NextUseLocked();
      if (output) *output = found->second.get();
      return InstalledAssetRenderResult::kOk;
    }
    ++stats_.cache_misses;
    std::shared_ptr<const InstalledAssetResource> resource;
    const InstalledAssetResult provider_result =
        provider_.LoadDynamicAnimation(source, &resource);
    if (provider_result != InstalledAssetResult::kOk || !resource) {
      SetErrorLocked("installed asset provider rejected dynamic item icon",
                     static_cast<long>(provider_result));
      return InstalledAssetRenderResult::kProviderFailure;
    }
    return DecodeAndCacheImageLocked(std::move(resource), cache_key, output);
  }

  InstalledAssetRenderResult AcquireFontLocked(InstalledAssetKey key,
                                                FontEntry** output) {
    const std::string cache_key = StaticCacheKey(key);
    auto found = fonts_.find(cache_key);
    if (found != fonts_.end()) {
      ++stats_.cache_hits;
      found->second->last_use = NextUseLocked();
      if (output) *output = found->second.get();
      return InstalledAssetRenderResult::kOk;
    }
    ++stats_.cache_misses;
    std::shared_ptr<const InstalledAssetResource> resource;
    const InstalledAssetResult provider_result = provider_.Load(key, &resource);
    if (provider_result != InstalledAssetResult::kOk || !resource) {
      SetErrorLocked("installed asset provider rejected font request",
                     static_cast<long>(provider_result));
      return InstalledAssetRenderResult::kProviderFailure;
    }
    return DecodeAndCacheFontLocked(std::move(resource), cache_key, output);
  }

  InstalledAssetRenderResult DecodeAndCacheImageLocked(
      std::shared_ptr<const InstalledAssetResource> resource,
      const std::string& cache_key,
      ImageEntry** output) {
    if (resource->metadata.kind != InstalledAssetKind::kAnimation) {
      SetErrorLocked("image request resolved to a non-animation asset");
      return InstalledAssetRenderResult::kWrongAssetKind;
    }
    const std::uint64_t expected_width =
        static_cast<std::uint64_t>(resource->metadata.frame_width) *
        resource->metadata.frame_count;
    if (resource->data.empty() || resource->metadata.frame_width == 0 ||
        resource->metadata.frame_height == 0 ||
        resource->metadata.frame_count == 0 || expected_width == 0 ||
        expected_width > config_.max_image_dimension ||
        resource->metadata.frame_height > config_.max_image_dimension ||
        expected_width > config_.max_image_pixels /
                             resource->metadata.frame_height) {
      ++stats_.decode_failures;
      SetErrorLocked("animation metadata exceeds renderer bounds");
      return InstalledAssetRenderResult::kDimensionMismatch;
    }

    if (resource->data.size() > static_cast<std::size_t>(MAXDWORD)) {
      ++stats_.decode_failures;
      SetErrorLocked("PNG byte count exceeds Win32 stream capacity");
      return InstalledAssetRenderResult::kDecodeFailure;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, resource->data.size());
    if (!memory) return CacheFailureLocked("GlobalAlloc failed for PNG stream");
    void* bytes = GlobalLock(memory);
    if (!bytes) {
      GlobalFree(memory);
      return CacheFailureLocked("GlobalLock failed for PNG stream");
    }
    std::memcpy(bytes, resource->data.data(), resource->data.size());
    GlobalUnlock(memory);
    IStream* stream = nullptr;
    const HRESULT stream_result = CreateStreamOnHGlobal(memory, TRUE, &stream);
    if (FAILED(stream_result) || !stream) {
      GlobalFree(memory);
      ++stats_.decode_failures;
      SetErrorLocked("CreateStreamOnHGlobal failed", stream_result);
      return InstalledAssetRenderResult::kDecodeFailure;
    }

    std::unique_ptr<Gdiplus::Bitmap> bitmap(
        Gdiplus::Bitmap::FromStream(stream, FALSE));
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
      const long status =
          bitmap ? static_cast<long>(bitmap->GetLastStatus()) : -1;
      bitmap.reset();
      stream->Release();
      ++stats_.decode_failures;
      SetErrorLocked("GDI+ rejected installed PNG bytes", status);
      return InstalledAssetRenderResult::kDecodeFailure;
    }
    if (bitmap->GetWidth() != expected_width ||
        bitmap->GetHeight() != resource->metadata.frame_height) {
      bitmap.reset();
      stream->Release();
      ++stats_.decode_failures;
      SetErrorLocked("decoded PNG dimensions do not match audited frames");
      return InstalledAssetRenderResult::kDimensionMismatch;
    }

    const std::uint64_t decoded_bytes64 =
        expected_width * resource->metadata.frame_height * 4ULL;
    if (decoded_bytes64 > std::numeric_limits<std::size_t>::max()) {
      bitmap.reset();
      stream->Release();
      return CacheFailureLocked("decoded PNG byte charge overflow");
    }
    std::size_t charge = 0;
    if (!CheckedAdd(resource->data.size(),
                    static_cast<std::size_t>(decoded_bytes64), &charge) ||
        !ReserveLocked(charge)) {
      bitmap.reset();
      stream->Release();
      return CacheFailureLocked("decoded PNG cannot fit renderer cache");
    }

    auto entry = std::make_unique<ImageEntry>();
    entry->stream = stream;
    entry->bitmap = std::move(bitmap);
    entry->frame_width = resource->metadata.frame_width;
    entry->frame_height = resource->metadata.frame_height;
    entry->frame_count = resource->metadata.frame_count;
    entry->charge = charge;
    entry->last_use = NextUseLocked();
    RemoveKeyLocked(cache_key);
    ImageEntry* raw = entry.get();
    images_.emplace(cache_key, std::move(entry));
    ++stats_.cached_images;
    stats_.cached_bytes += charge;
    if (output) *output = raw;
    return InstalledAssetRenderResult::kOk;
  }

  InstalledAssetRenderResult DecodeAndCacheFontLocked(
      std::shared_ptr<const InstalledAssetResource> resource,
      const std::string& cache_key,
      FontEntry** output) {
    if (resource->metadata.kind != InstalledAssetKind::kFont) {
      SetErrorLocked("font request resolved to a non-font asset");
      return InstalledAssetRenderResult::kWrongAssetKind;
    }
    if (resource->data.empty() ||
        resource->data.size() > static_cast<std::size_t>(MAXINT)) {
      ++stats_.decode_failures;
      SetErrorLocked("font byte count is invalid for GDI+");
      return InstalledAssetRenderResult::kFontLoadFailure;
    }
    const std::size_t charge = resource->data.size();
    if (!ReserveLocked(charge)) {
      return CacheFailureLocked("installed font cannot fit renderer cache");
    }
    auto entry = std::make_unique<FontEntry>();
    entry->bytes = resource->data;
    entry->collection =
        std::make_unique<Gdiplus::PrivateFontCollection>();
    Gdiplus::Status status = entry->collection->AddMemoryFont(
        entry->bytes.data(), static_cast<INT>(entry->bytes.size()));
    if (status != Gdiplus::Ok || entry->collection->GetFamilyCount() < 1) {
      ++stats_.decode_failures;
      SetErrorLocked("GDI+ rejected installed font bytes", status);
      return InstalledAssetRenderResult::kFontLoadFailure;
    }
    const INT family_count = entry->collection->GetFamilyCount();
    std::unique_ptr<Gdiplus::FontFamily[]> families(
        new Gdiplus::FontFamily[static_cast<std::size_t>(family_count)]);
    INT found = 0;
    status = entry->collection->GetFamilies(family_count, families.get(), &found);
    if (status != Gdiplus::Ok || found < 1 || !families[0].IsAvailable()) {
      ++stats_.decode_failures;
      SetErrorLocked("installed font exposes no usable family", status);
      return InstalledAssetRenderResult::kFontLoadFailure;
    }
    WCHAR family_name[LF_FACESIZE] = {};
    status = families[0].GetFamilyName(family_name);
    if (status != Gdiplus::Ok || family_name[0] == L'\0') {
      ++stats_.decode_failures;
      SetErrorLocked("installed font family name is unavailable", status);
      return InstalledAssetRenderResult::kFontLoadFailure;
    }
    entry->family = std::make_unique<Gdiplus::FontFamily>(
        family_name, entry->collection.get());
    if (!entry->family->IsAvailable()) {
      ++stats_.decode_failures;
      SetErrorLocked("installed private font family could not be reopened");
      return InstalledAssetRenderResult::kFontLoadFailure;
    }
    entry->charge = charge;
    entry->last_use = NextUseLocked();
    RemoveKeyLocked(cache_key);
    FontEntry* raw = entry.get();
    fonts_.emplace(cache_key, std::move(entry));
    ++stats_.cached_fonts;
    stats_.cached_bytes += charge;
    if (output) *output = raw;
    return InstalledAssetRenderResult::kOk;
  }

  InstalledAssetRenderResult DrawOrMeasureTextLocked(
      HDC dc, const FontEntry& entry, std::wstring_view text,
      const InstalledAssetTextStyle& style, const RECT* layout,
      SIZE* measured_pixels) {
    int requested_style = FontStyleValue(style);
    if (!entry.family->IsStyleAvailable(requested_style)) {
      requested_style = Gdiplus::FontStyleRegular;
    }
    Gdiplus::Font font(entry.family.get(), style.pixel_size, requested_style,
                       Gdiplus::UnitPixel);
    if (font.GetLastStatus() != Gdiplus::Ok) {
      return DrawFailureLocked("GDI+ could not construct installed font",
                               font.GetLastStatus());
    }
    Gdiplus::Graphics graphics(dc);
    if (graphics.GetLastStatus() != Gdiplus::Ok) {
      return DrawFailureLocked("GDI+ could not bind text HDC",
                               graphics.GetLastStatus());
    }
    graphics.SetTextRenderingHint(
        Gdiplus::TextRenderingHintSingleBitPerPixelGridFit);
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    format.SetFormatFlags(format.GetFormatFlags() |
                          Gdiplus::StringFormatFlagsMeasureTrailingSpaces |
                          (style.wrap ? 0 : Gdiplus::StringFormatFlagsNoWrap));
    if (style.ellipsis) {
      format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    }
    switch (style.alignment) {
      case InstalledTextAlignment::kCenter:
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        break;
      case InstalledTextAlignment::kRight:
        format.SetAlignment(Gdiplus::StringAlignmentFar);
        break;
      case InstalledTextAlignment::kLeft:
        format.SetAlignment(Gdiplus::StringAlignmentNear);
        break;
    }
    const INT length = static_cast<INT>(text.size());
    if (layout) {
      Gdiplus::SolidBrush brush(Gdiplus::Color(style.argb));
      const Gdiplus::RectF box(
          static_cast<Gdiplus::REAL>(layout->left),
          static_cast<Gdiplus::REAL>(layout->top),
          static_cast<Gdiplus::REAL>(layout->right - layout->left),
          static_cast<Gdiplus::REAL>(layout->bottom - layout->top));
      const Gdiplus::Status status =
          graphics.DrawString(text.data(), length, &font, box, &format, &brush);
      if (status != Gdiplus::Ok) {
        return DrawFailureLocked("GDI+ DrawString failed", status);
      }
      return InstalledAssetRenderResult::kOk;
    }
    Gdiplus::RectF bounds;
    const Gdiplus::Status status = graphics.MeasureString(
        text.data(), length, &font, Gdiplus::PointF(0.0F, 0.0F), &format,
        &bounds);
    if (status != Gdiplus::Ok || !std::isfinite(bounds.Width) ||
        !std::isfinite(bounds.Height) || bounds.Width < 0.0F ||
        bounds.Height < 0.0F ||
        bounds.Width > static_cast<Gdiplus::REAL>(LONG_MAX) ||
        bounds.Height > static_cast<Gdiplus::REAL>(LONG_MAX)) {
      return DrawFailureLocked("GDI+ MeasureString failed", status);
    }
    measured_pixels->cx = static_cast<LONG>(std::ceil(bounds.Width));
    measured_pixels->cy = static_cast<LONG>(std::ceil(bounds.Height));
    return InstalledAssetRenderResult::kOk;
  }

  bool ValidTextLocked(std::wstring_view text,
                       const InstalledAssetTextStyle& style) {
    return text.size() <= config_.max_text_characters &&
           text.size() <= static_cast<std::size_t>(MAXINT) &&
           std::isfinite(style.pixel_size) &&
           style.pixel_size >= config_.minimum_font_pixels &&
           style.pixel_size <= config_.maximum_font_pixels;
  }

  bool ReserveLocked(std::size_t charge) {
    if (charge > config_.max_cached_bytes) return false;
    while (stats_.cached_images + stats_.cached_fonts >=
               config_.max_cached_items ||
           stats_.cached_bytes > config_.max_cached_bytes - charge) {
      if (!EvictOldestLocked()) return false;
    }
    return true;
  }

  bool EvictOldestLocked() {
    bool image_selected = false;
    std::string selected_key;
    std::uint64_t selected_use = std::numeric_limits<std::uint64_t>::max();
    for (const auto& pair : images_) {
      if (pair.second->last_use < selected_use) {
        selected_use = pair.second->last_use;
        selected_key = pair.first;
        image_selected = true;
      }
    }
    for (const auto& pair : fonts_) {
      if (pair.second->last_use < selected_use) {
        selected_use = pair.second->last_use;
        selected_key = pair.first;
        image_selected = false;
      }
    }
    if (selected_use == std::numeric_limits<std::uint64_t>::max()) return false;
    if (image_selected) {
      auto found = images_.find(selected_key);
      stats_.cached_bytes -= found->second->charge;
      images_.erase(found);
      --stats_.cached_images;
    } else {
      auto found = fonts_.find(selected_key);
      stats_.cached_bytes -= found->second->charge;
      fonts_.erase(found);
      --stats_.cached_fonts;
    }
    ++stats_.evictions;
    return true;
  }

  void RemoveKeyLocked(const std::string& key) {
    auto image = images_.find(key);
    if (image != images_.end()) {
      stats_.cached_bytes -= image->second->charge;
      images_.erase(image);
      --stats_.cached_images;
    }
    auto font = fonts_.find(key);
    if (font != fonts_.end()) {
      stats_.cached_bytes -= font->second->charge;
      fonts_.erase(font);
      --stats_.cached_fonts;
    }
  }

  std::uint64_t NextUseLocked() {
    if (use_counter_ == std::numeric_limits<std::uint64_t>::max()) {
      std::uint64_t next = 1;
      for (auto& pair : images_) pair.second->last_use = next++;
      for (auto& pair : fonts_) pair.second->last_use = next++;
      use_counter_ = next;
    }
    return ++use_counter_;
  }

  InstalledAssetRenderResult NotInitializedLocked() {
    SetErrorLocked("installed asset renderer is not initialized");
    return InstalledAssetRenderResult::kNotInitialized;
  }

  InstalledAssetRenderResult CacheFailureLocked(const char* message) {
    SetErrorLocked(message);
    return InstalledAssetRenderResult::kCacheFull;
  }

  InstalledAssetRenderResult DrawFailureLocked(const char* message,
                                                long detail = 0) {
    ++stats_.draw_failures;
    SetErrorLocked(message, detail);
    return InstalledAssetRenderResult::kDrawFailure;
  }

  InstalledAssetRenderResult AllocationFailure() {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      last_error_ = "allocation failed in installed asset renderer";
    } catch (...) {
    }
    return InstalledAssetRenderResult::kCacheFull;
  }

  InstalledAssetRenderResult UnexpectedFailure() {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      last_error_ = "unexpected exception in installed asset renderer";
    } catch (...) {
    }
    return InstalledAssetRenderResult::kDecodeFailure;
  }

  void SetErrorLocked(const char* message, long detail = 0) {
    last_error_ = message ? message : "installed asset renderer failure";
    if (detail != 0) {
      last_error_ += " (status=" + std::to_string(detail) + ")";
    }
  }

  InstalledAssetProvider& provider_;
  InstalledAssetRendererConfig config_;
  mutable std::mutex mutex_;
  ULONG_PTR gdiplus_token_ = 0;
  bool initialized_ = false;
  std::uint64_t use_counter_ = 0;
  std::unordered_map<std::string, std::unique_ptr<ImageEntry>> images_;
  std::unordered_map<std::string, std::unique_ptr<FontEntry>> fonts_;
  InstalledAssetRendererStats stats_;
  std::string last_error_;
};

InstalledAssetRenderer::InstalledAssetRenderer(
    InstalledAssetProvider& provider, InstalledAssetRendererConfig config)
    : impl_(std::make_unique<Impl>(provider, config)) {}

InstalledAssetRenderer::~InstalledAssetRenderer() = default;

InstalledAssetRenderResult InstalledAssetRenderer::Initialize() {
  return impl_->Initialize();
}

void InstalledAssetRenderer::Shutdown() { impl_->Shutdown(); }

bool InstalledAssetRenderer::IsInitialized() const {
  return impl_->IsInitialized();
}

InstalledAssetRenderResult InstalledAssetRenderer::PrefetchImage(
    InstalledAssetKey key) {
  return impl_->Prefetch(key, InstalledAssetKind::kAnimation);
}

InstalledAssetRenderResult InstalledAssetRenderer::PrefetchFont(
    InstalledAssetKey key) {
  return impl_->Prefetch(key, InstalledAssetKind::kFont);
}

InstalledAssetRenderResult InstalledAssetRenderer::PrefetchItemIcon(
    const InstalledItemIconRef& source) {
  return impl_->PrefetchItemIcon(source);
}

InstalledAssetRenderResult InstalledAssetRenderer::PrefetchDynamicImage(
    const InstalledDynamicAnimationRef& source) {
  return impl_->PrefetchDynamicImage(source);
}

InstalledAssetRenderResult InstalledAssetRenderer::DrawFrame(
    HDC destination_dc, InstalledAssetKey key, std::uint32_t frame_index,
    const RECT& destination, const InstalledAssetFrameStyle& style) {
  return impl_->DrawFrame(destination_dc, key, frame_index, destination, style);
}

InstalledAssetRenderResult InstalledAssetRenderer::DrawItemIconFrame(
    HDC destination_dc, const InstalledItemIconRef& source,
    std::uint32_t frame_index, const RECT& destination,
    const InstalledAssetFrameStyle& style) {
  return impl_->DrawItemIconFrame(destination_dc, source, frame_index,
                                  destination, style);
}

InstalledAssetRenderResult InstalledAssetRenderer::DrawDynamicFrame(
    HDC destination_dc, const InstalledDynamicAnimationRef& source,
    std::uint32_t frame_index, const RECT& destination,
    const InstalledAssetFrameStyle& style) {
  return impl_->DrawDynamicFrame(destination_dc, source, frame_index,
                                 destination, style);
}

InstalledAssetRenderResult InstalledAssetRenderer::DrawText(
    HDC destination_dc, InstalledAssetKey font_key, std::wstring_view text,
    const RECT& layout, const InstalledAssetTextStyle& style) {
  return impl_->DrawText(destination_dc, font_key, text, layout, style);
}

InstalledAssetRenderResult InstalledAssetRenderer::MeasureText(
    HDC reference_dc, InstalledAssetKey font_key, std::wstring_view text,
    const InstalledAssetTextStyle& style, SIZE* measured_pixels) {
  return impl_->MeasureText(reference_dc, font_key, text, style,
                            measured_pixels);
}

InstalledAssetRendererStats InstalledAssetRenderer::Stats() const {
  return impl_->Stats();
}

std::string InstalledAssetRenderer::LastError() const {
  return impl_->LastError();
}

#ifdef FOMDS_ASSET_RENDERER_TESTING
InstalledAssetRenderResult InstalledAssetRenderer::InjectResourceForTests(
    std::shared_ptr<const InstalledAssetResource> resource) {
  return impl_->InjectResourceForTests(std::move(resource));
}
#endif

}  // namespace fomds

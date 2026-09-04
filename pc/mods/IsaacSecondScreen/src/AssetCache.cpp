#include "AssetCache.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "Log.h"

namespace isaacds {
namespace {

ULONG_PTR g_gdiplus_token = 0;
std::wstring g_asset_root;
std::unordered_map<std::string, std::unique_ptr<Gdiplus::Image>> g_images;
std::unordered_set<std::string> g_missing;

bool ValidRelativePath(const std::string& value) {
  if (value.empty() || value.size() > 512 || value.front() == '/' ||
      value.front() == '\\' || value.find(':') != std::string::npos ||
      value.find('\0') != std::string::npos)
    return false;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t end = value.find_first_of("/\\", start);
    const std::string_view segment(
        value.data() + start,
        (end == std::string::npos ? value.size() : end) - start);
    if (segment.empty() || segment == "." || segment == "..") return false;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return true;
}

std::wstring Wide(const std::string& text) {
  if (text.empty()) return {};
  const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
  if (required <= 0) return {};
  std::wstring result(static_cast<size_t>(required), L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                           static_cast<int>(text.size()), result.data(),
                           required))
    return {};
  return result;
}

bool Draw(HDC dc, Gdiplus::Image* image, const RECT& destination,
          int source_x, int source_y, int source_width, int source_height,
          bool smooth) {
  if (!dc || !image || destination.right <= destination.left ||
      destination.bottom <= destination.top || source_width <= 0 ||
      source_height <= 0)
    return false;
  Gdiplus::Graphics graphics(dc);
  graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
  graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
  graphics.SetInterpolationMode(smooth ? Gdiplus::InterpolationModeBilinear
                                       : Gdiplus::InterpolationModeNearestNeighbor);
  graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  const Gdiplus::Rect target(destination.left, destination.top,
                             destination.right - destination.left,
                             destination.bottom - destination.top);
  return graphics.DrawImage(image, target, source_x, source_y, source_width,
                            source_height, Gdiplus::UnitPixel) == Gdiplus::Ok;
}

}  // namespace

bool InitializeAssetCache(HMODULE module) {
  if (g_gdiplus_token) return true;
  if (!module) return false;
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
  if (!length || length >= MAX_PATH) return false;
  wchar_t* slash = wcsrchr(path, L'\\');
  if (!slash) return false;
  *(slash + 1) = L'\0';
  const std::wstring module_directory(path);
  const std::wstring adjacent =
      module_directory + L"isaac_second_screen_assets\\";
  const std::wstring installed =
      module_directory + L"resources\\scripts\\isaac_second_screen_assets\\";
  const std::wstring adjacent_catalog = adjacent + L"catalog.tsv";
  g_asset_root =
      GetFileAttributesW(adjacent_catalog.c_str()) != INVALID_FILE_ATTRIBUTES
          ? adjacent
          : installed;
  Gdiplus::GdiplusStartupInput input;
  if (Gdiplus::GdiplusStartup(&g_gdiplus_token, &input, nullptr) !=
      Gdiplus::Ok) {
    g_gdiplus_token = 0;
    g_asset_root.clear();
    return false;
  }
  return true;
}

void ShutdownAssetCache() {
  g_images.clear();
  g_missing.clear();
  if (g_gdiplus_token) Gdiplus::GdiplusShutdown(g_gdiplus_token);
  g_gdiplus_token = 0;
  g_asset_root.clear();
}

const std::wstring& AssetRootPath() { return g_asset_root; }

Gdiplus::Image* ResolveAsset(const std::string& relative_path) {
  if (!g_gdiplus_token || !ValidRelativePath(relative_path)) return nullptr;
  const auto existing = g_images.find(relative_path);
  if (existing != g_images.end()) return existing->second.get();
  if (g_missing.find(relative_path) != g_missing.end()) return nullptr;
  std::wstring path = g_asset_root;
  std::wstring relative = Wide(relative_path);
  if (relative.empty()) return nullptr;
  std::replace(relative.begin(), relative.end(), L'/', L'\\');
  path += relative;
  auto image = std::make_unique<Gdiplus::Image>(path.c_str(), FALSE);
  if (image->GetLastStatus() != Gdiplus::Ok || !image->GetWidth() ||
      !image->GetHeight()) {
    g_missing.insert(relative_path);
    LogRateLimited("asset-missing", 5000, LogLevel::kWarning,
                   "[Assets] unavailable path=%s", relative_path.c_str());
    return nullptr;
  }
  Gdiplus::Image* result = image.get();
  g_images.emplace(relative_path, std::move(image));
  return result;
}

bool DrawAssetStretch(HDC dc, const std::string& relative_path,
                      const RECT& bounds, bool smooth) {
  Gdiplus::Image* image = ResolveAsset(relative_path);
  if (!image) return false;
  return Draw(dc, image, bounds, 0, 0, static_cast<int>(image->GetWidth()),
              static_cast<int>(image->GetHeight()), smooth);
}

bool DrawAssetCover(HDC dc, const std::string& relative_path,
                    const RECT& bounds, bool smooth) {
  Gdiplus::Image* image = ResolveAsset(relative_path);
  if (!image || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
    return false;
  const int destination_width = bounds.right - bounds.left;
  const int destination_height = bounds.bottom - bounds.top;
  const double destination_ratio =
      static_cast<double>(destination_width) / destination_height;
  const double image_ratio =
      static_cast<double>(image->GetWidth()) / image->GetHeight();
  int source_x = 0;
  int source_y = 0;
  int source_width = static_cast<int>(image->GetWidth());
  int source_height = static_cast<int>(image->GetHeight());
  if (image_ratio > destination_ratio) {
    source_width = std::max(
        1, static_cast<int>(static_cast<double>(source_height) *
                            destination_ratio));
    source_x = (static_cast<int>(image->GetWidth()) - source_width) / 2;
  } else if (image_ratio < destination_ratio) {
    source_height = std::max(
        1, static_cast<int>(static_cast<double>(source_width) /
                            destination_ratio));
    source_y = (static_cast<int>(image->GetHeight()) - source_height) / 2;
  }
  return Draw(dc, image, bounds, source_x, source_y, source_width,
              source_height, smooth);
}

bool DrawAssetFit(HDC dc, const std::string& relative_path,
                  const RECT& bounds, bool smooth) {
  Gdiplus::Image* image = ResolveAsset(relative_path);
  if (!image || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
    return false;
  const int available_width = bounds.right - bounds.left;
  const int available_height = bounds.bottom - bounds.top;
  const double scale = std::min(
      static_cast<double>(available_width) / image->GetWidth(),
      static_cast<double>(available_height) / image->GetHeight());
  const int width =
      std::max(1, static_cast<int>(image->GetWidth() * scale));
  const int height =
      std::max(1, static_cast<int>(image->GetHeight() * scale));
  RECT destination = {bounds.left + (available_width - width) / 2,
                      bounds.top + (available_height - height) / 2,
                      bounds.left + (available_width + width) / 2,
                      bounds.top + (available_height + height) / 2};
  return Draw(dc, image, destination, 0, 0,
              static_cast<int>(image->GetWidth()),
              static_cast<int>(image->GetHeight()), smooth);
}

bool DrawAssetRegion(HDC dc, const std::string& relative_path,
                     const RECT& source_bounds, const RECT& destination,
                     bool smooth) {
  Gdiplus::Image* image = ResolveAsset(relative_path);
  if (!image || source_bounds.left < 0 || source_bounds.top < 0 ||
      source_bounds.right > static_cast<int>(image->GetWidth()) ||
      source_bounds.bottom > static_cast<int>(image->GetHeight()))
    return false;
  return Draw(dc, image, destination, source_bounds.left, source_bounds.top,
              source_bounds.right - source_bounds.left,
              source_bounds.bottom - source_bounds.top, smooth);
}

bool DrawSprite(HDC dc, const SpriteFrame* frame, const RECT& bounds) {
  if (!frame) return false;
  Gdiplus::Image* image = ResolveAsset(frame->asset);
  return image && Draw(dc, image, bounds, frame->x, frame->y, frame->width,
                       frame->height, false);
}

}  // namespace isaacds

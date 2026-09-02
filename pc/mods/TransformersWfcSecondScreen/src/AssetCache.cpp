#include "AssetCache.h"

#include <wtypes.h>
#include <gdiplus.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Log.h"

namespace wfcds {
namespace {

ULONG_PTR g_gdiplus_token = 0;
std::wstring g_asset_directory;
std::unordered_map<std::string, std::unique_ptr<Gdiplus::Image>> g_images;
std::unordered_set<std::string> g_missing;

bool ValidKey(const char* key) {
  if (!key || !*key) return false;
  size_t length = 0;
  for (const unsigned char* cursor =
           reinterpret_cast<const unsigned char*>(key);
       *cursor; ++cursor) {
    if (++length > 127) return false;
    if (!(std::isalnum(*cursor) || *cursor == '_' || *cursor == '-' ||
          *cursor == ' '))
      return false;
  }
  return true;
}

std::wstring Wide(const char* text) {
  if (!text || !*text) return {};
  const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
                                           -1, nullptr, 0);
  if (required <= 1) return {};
  std::wstring result(static_cast<size_t>(required), L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                           result.data(), required))
    return {};
  result.pop_back();
  return result;
}

Gdiplus::Image* Resolve(const char* key) {
  if (!g_gdiplus_token || !ValidKey(key)) return nullptr;
  const std::string normalized(key);
  const auto existing = g_images.find(normalized);
  if (existing != g_images.end()) return existing->second.get();
  if (g_missing.find(normalized) != g_missing.end()) return nullptr;

  const std::wstring wide_key = Wide(key);
  if (wide_key.empty()) {
    g_missing.insert(normalized);
    return nullptr;
  }
  const std::wstring path = g_asset_directory + wide_key + L".png";
  auto image = std::make_unique<Gdiplus::Image>(path.c_str(), FALSE);
  if (image->GetLastStatus() != Gdiplus::Ok || !image->GetWidth() ||
      !image->GetHeight()) {
    g_missing.insert(normalized);
    LogRateLimited("asset-missing", 60000, LogLevel::kWarning,
                   "[DualScreen] authentic asset unavailable key=%s", key);
    return nullptr;
  }
  Gdiplus::Image* result = image.get();
  g_images.emplace(normalized, std::move(image));
  return result;
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
  g_asset_directory = path;
  g_asset_directory += L"assets\\";

  Gdiplus::GdiplusStartupInput input;
  if (Gdiplus::GdiplusStartup(&g_gdiplus_token, &input, nullptr) !=
      Gdiplus::Ok) {
    g_gdiplus_token = 0;
    g_asset_directory.clear();
    return false;
  }
  return true;
}

void ShutdownAssetCache() {
  g_images.clear();
  g_missing.clear();
  if (g_gdiplus_token) Gdiplus::GdiplusShutdown(g_gdiplus_token);
  g_gdiplus_token = 0;
  g_asset_directory.clear();
}

bool DrawCachedAsset(HDC dc, const char* key, const RECT& bounds) {
  Gdiplus::Image* image = Resolve(key);
  if (!dc || !image || bounds.right <= bounds.left ||
      bounds.bottom <= bounds.top)
    return false;

  const int available_width = bounds.right - bounds.left;
  const int available_height = bounds.bottom - bounds.top;
  const double scale = std::min(
      static_cast<double>(available_width) / image->GetWidth(),
      static_cast<double>(available_height) / image->GetHeight());
  const int width = std::max(1, static_cast<int>(image->GetWidth() * scale));
  const int height = std::max(1, static_cast<int>(image->GetHeight() * scale));
  const int left = bounds.left + (available_width - width) / 2;
  const int top = bounds.top + (available_height - height) / 2;

  Gdiplus::Graphics graphics(dc);
  graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
  graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
  return graphics.DrawImage(image, left, top, width, height) == Gdiplus::Ok;
}

bool DrawCachedAssetCover(HDC dc, const char* key, const RECT& bounds) {
  Gdiplus::Image* image = Resolve(key);
  if (!dc || !image || bounds.right <= bounds.left ||
      bounds.bottom <= bounds.top)
    return false;

  const int available_width = bounds.right - bounds.left;
  const int available_height = bounds.bottom - bounds.top;
  const double scale = std::max(
      static_cast<double>(available_width) / image->GetWidth(),
      static_cast<double>(available_height) / image->GetHeight());
  const int width = std::max(1, static_cast<int>(image->GetWidth() * scale));
  const int height = std::max(1, static_cast<int>(image->GetHeight() * scale));
  const int left = bounds.left + (available_width - width) / 2;
  const int top = bounds.top + (available_height - height) / 2;

  const int saved = SaveDC(dc);
  if (!saved) return false;
  IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
  bool drawn = false;
  {
    Gdiplus::Graphics graphics(dc);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    drawn = graphics.DrawImage(image, left, top, width, height) == Gdiplus::Ok;
  }
  RestoreDC(dc, saved);
  return drawn;
}

}  // namespace wfcds

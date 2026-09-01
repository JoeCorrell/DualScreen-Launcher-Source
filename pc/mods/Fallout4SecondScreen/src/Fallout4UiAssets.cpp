#include "Fallout4UiAssets.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Log.h"

namespace fo4ds {
namespace {

constexpr uint32_t kRgbaMagic = 0x41424752u;  // "RGBA" little endian.
constexpr uint32_t kMaxAssetSide = 2048;
constexpr uint64_t kMaxAssetBytes = 16ull * 1024ull * 1024ull;
constexpr size_t kMaxCachedAssets = 48;
constexpr size_t kMaxCachedMisses = 512;
constexpr COLORREF kPipboyTint = RGB(86, 246, 146);

uint32_t ReadU32(const uint8_t* source) {
  uint32_t value = 0;
  std::memcpy(&value, source, sizeof(value));
  return value;
}

std::wstring ExecutableDirectory() {
  std::vector<wchar_t> path(32768);
  const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                          static_cast<DWORD>(path.size()));
  if (!length || length >= path.size()) return {};
  std::wstring result(path.data(), length);
  const size_t separator = result.find_last_of(L"\\/");
  if (separator == std::wstring::npos) return {};
  result.resize(separator);
  return result;
}

std::string CanonicalSwfPath(const char* source) {
  if (!source || !*source) return {};
  std::string path(source);
  if (path.size() > 512) return {};
  for (char& value : path) {
    if (value == '/') value = '\\';
    value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  }
  while (!path.empty() && path.front() == '\\') path.erase(path.begin());
  if (path.find("..") != std::string::npos || path.find(':') != std::string::npos)
    return {};
  constexpr const char kInterface[] = "interface\\";
  if (path.compare(0, sizeof(kInterface) - 1, kInterface) != 0)
    path.insert(0, kInterface);
  return path;
}

uint64_t Fnv1a64(const std::string& value) {
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char byte : value) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::wstring Hex16(uint64_t value) {
  constexpr wchar_t digits[] = L"0123456789abcdef";
  std::wstring result(16, L'0');
  for (int index = 15; index >= 0; --index) {
    result[static_cast<size_t>(index)] = digits[value & 0xfu];
    value >>= 4;
  }
  return result;
}

struct Dib {
  HDC dc = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previous = nullptr;
  int width = 0;
  int height = 0;

  ~Dib() {
    if (dc && previous && previous != HGDI_ERROR) SelectObject(dc, previous);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
  }
  Dib(const Dib&) = delete;
  Dib& operator=(const Dib&) = delete;
  Dib() = default;
};

std::unique_ptr<Dib> LoadRgba(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return nullptr;
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 12 ||
      static_cast<uint64_t>(size.QuadPart) > kMaxAssetBytes + 12u) {
    CloseHandle(file);
    return nullptr;
  }
  std::vector<uint8_t> bytes;
  try {
    bytes.resize(static_cast<size_t>(size.QuadPart));
  } catch (...) {
    CloseHandle(file);
    return nullptr;
  }
  DWORD read = 0;
  const bool read_ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                                &read, nullptr) != FALSE;
  CloseHandle(file);
  if (!read_ok || read != bytes.size() || ReadU32(bytes.data()) != kRgbaMagic)
    return nullptr;
  const uint32_t width = ReadU32(bytes.data() + 4);
  const uint32_t height = ReadU32(bytes.data() + 8);
  const uint64_t pixels = static_cast<uint64_t>(width) * height;
  if (!width || !height || width > kMaxAssetSide || height > kMaxAssetSide ||
      pixels > kMaxAssetBytes / 4u || bytes.size() != 12u + pixels * 4u)
    return nullptr;

  std::unique_ptr<Dib> dib(new (std::nothrow) Dib());
  if (!dib) return nullptr;
  dib->dc = CreateCompatibleDC(nullptr);
  if (!dib->dc) return nullptr;
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = static_cast<LONG>(width);
  info.bmiHeader.biHeight = -static_cast<LONG>(height);
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* raw = nullptr;
  dib->bitmap = CreateDIBSection(dib->dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
  if (!dib->bitmap || !raw) return nullptr;
  dib->previous = SelectObject(dib->dc, dib->bitmap);
  if (!dib->previous || dib->previous == HGDI_ERROR) return nullptr;
  dib->width = static_cast<int>(width);
  dib->height = static_cast<int>(height);

  const uint32_t tint_red = GetRValue(kPipboyTint);
  const uint32_t tint_green = GetGValue(kPipboyTint);
  const uint32_t tint_blue = GetBValue(kPipboyTint);
  uint8_t* destination = static_cast<uint8_t*>(raw);
  const uint8_t* source = bytes.data() + 12;
  for (uint64_t index = 0; index < pixels; ++index) {
    const uint32_t red = source[index * 4];
    const uint32_t green = source[index * 4 + 1];
    const uint32_t blue = source[index * 4 + 2];
    const uint32_t alpha = source[index * 4 + 3];
    const uint32_t luminance = std::max({red, green, blue});
    const uint32_t effective_alpha = (alpha * luminance + 127u) / 255u;
    destination[index * 4] =
        static_cast<uint8_t>((tint_blue * effective_alpha + 127u) / 255u);
    destination[index * 4 + 1] =
        static_cast<uint8_t>((tint_green * effective_alpha + 127u) / 255u);
    destination[index * 4 + 2] =
        static_cast<uint8_t>((tint_red * effective_alpha + 127u) / 255u);
    destination[index * 4 + 3] = static_cast<uint8_t>(effective_alpha);
  }
  return dib;
}

struct Cache {
  std::mutex mutex;
  bool initialized = false;
  std::wstring root;
  std::unordered_map<std::wstring, std::unique_ptr<Dib>> assets;
  std::unordered_set<std::wstring> misses;
};

Cache& State() {
  static Cache* cache = new (std::nothrow) Cache();
  static Cache fallback;
  return cache ? *cache : fallback;
}

bool InitializeLocked(Cache& cache) {
  if (cache.initialized) return !cache.root.empty();
  cache.initialized = true;
  const std::wstring game = ExecutableDirectory();
  if (game.empty()) return false;
  cache.root = game + L"\\Fallout4SecondScreen.assets";
  const DWORD attributes = GetFileAttributesW(cache.root.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    Log(LogLevel::kWarning,
        "[ART] user-owned Fallout 4 artwork cache is unavailable");
    cache.root.clear();
    return false;
  }
  Log(LogLevel::kInfo,
      "[ART] user-owned Fallout 4 artwork cache initialized");
  return true;
}

Dib* GetLocked(Cache& cache, const std::wstring& relative) {
  if (!InitializeLocked(cache) || relative.empty() ||
      relative.find(L"..") != std::wstring::npos ||
      relative.find(L':') != std::wstring::npos)
    return nullptr;
  const auto existing = cache.assets.find(relative);
  if (existing != cache.assets.end()) return existing->second.get();
  if (cache.misses.find(relative) != cache.misses.end()) return nullptr;
  std::unique_ptr<Dib> loaded = LoadRgba(cache.root + L"\\" + relative);
  if (!loaded) {
    if (cache.misses.size() < kMaxCachedMisses) cache.misses.insert(relative);
    return nullptr;
  }
  if (cache.assets.size() >= kMaxCachedAssets) cache.assets.erase(cache.assets.begin());
  Dib* result = loaded.get();
  cache.assets.emplace(relative, std::move(loaded));
  return result;
}

bool DrawAsset(HDC target, const std::wstring& relative,
               const Fallout4UiAssetBox& bounds) {
  if (!target || bounds.w <= 0 || bounds.h <= 0) return false;
  Cache& cache = State();
  std::lock_guard<std::mutex> lock(cache.mutex);
  Dib* dib = GetLocked(cache, relative);
  if (!dib) return false;
  const float scale = std::min(static_cast<float>(bounds.w) / dib->width,
                               static_cast<float>(bounds.h) / dib->height);
  const int width = std::max(1, static_cast<int>(dib->width * scale));
  const int height = std::max(1, static_cast<int>(dib->height * scale));
  const int x = bounds.x + (bounds.w - width) / 2;
  const int y = bounds.y + (bounds.h - height) / 2;
  const BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  return AlphaBlend(target, x, y, width, height, dib->dc, 0, 0,
                    dib->width, dib->height, blend) != FALSE;
}

std::wstring PerkPath(const char* swf_file, uint32_t clip_name,
                      uint32_t form_id) {
  std::string canonical = CanonicalSwfPath(swf_file);
  if (canonical.empty()) {
    const uint32_t identifier = clip_name ? clip_name : form_id;
    if (!identifier) return {};
    char fallback[96] = {};
    std::snprintf(fallback, sizeof(fallback),
                  "interface\\components\\vaultboys\\perks\\perkclip_%x.swf",
                  identifier);
    canonical = fallback;
  }
  return L"perks\\" + Hex16(Fnv1a64(canonical)) + L".rgba";
}

}  // namespace

bool DrawFallout4SpecialArt(HDC target, uint32_t special_index,
                            const Fallout4UiAssetBox& bounds) {
  if (special_index >= 7) return false;
  return DrawAsset(target, L"special\\" + std::to_wstring(special_index) +
                               L".rgba", bounds);
}

bool DrawFallout4ConditionArt(HDC target, uint32_t body_flags,
                              uint32_t head_flags,
                              const Fallout4UiAssetBox& bounds) {
  const uint32_t body = body_flags <= 16 ? body_flags : 0;
  const uint32_t head = head_flags <= 31 ? head_flags : 0;
  const bool body_drawn = DrawAsset(
      target, L"condition\\body_" + std::to_wstring(body) + L".rgba", bounds);
  if (!body_drawn) return false;
  DrawAsset(target, L"condition\\head_" + std::to_wstring(head) + L".rgba",
            bounds);
  return true;
}

bool DrawFallout4PerkArt(HDC target, uint32_t clip_name, uint32_t form_id,
                         const char* swf_file,
                         const Fallout4UiAssetBox& bounds) {
  return DrawAsset(target, PerkPath(swf_file, clip_name, form_id), bounds);
}

void ShutdownFallout4UiAssets() {
  Cache& cache = State();
  std::lock_guard<std::mutex> lock(cache.mutex);
  cache.assets.clear();
  cache.misses.clear();
  cache.root.clear();
  cache.initialized = false;
}

}  // namespace fo4ds

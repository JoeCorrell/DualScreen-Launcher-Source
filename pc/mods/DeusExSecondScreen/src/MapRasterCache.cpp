#include "MapRasterCache.h"

#include <windows.h>
#include <d3d11.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

#include "Log.h"

namespace dxs {
namespace {

constexpr char kMagic[8] = {'D', 'X', 'H', 'R', 'M', 'A', 'P', '\0'};
constexpr uint32_t kVersion = 1;
constexpr uint32_t kRgba8 = 1;
constexpr uint32_t kMaxDimension = 8192;
constexpr uint64_t kMaxRawBytes = 512ull * 1024ull * 1024ull;

#pragma pack(push, 1)
struct FileHeader {
  char magic[8];
  uint32_t version;
  uint32_t header_bytes;
  uint32_t map_dtp_id;
  int32_t floor;
  uint32_t width;
  uint32_t height;
  uint32_t pixel_format;
  uint32_t raw_bytes;
  uint32_t compressed_bytes;
  uint32_t raw_crc32;
  uint32_t swf_dtp_id;
  uint32_t source_crc32;
  uint32_t reserved0;
  uint32_t reserved1;
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 64, "DXHR map cache header changed");

bool ReadRasterHeader(const std::wstring& path, uint32_t requested_map,
                      int requested_floor, FileHeader* output) {
  if (!output) return false;
  *output = {};
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {};
  FileHeader header = {};
  DWORD received = 0;
  const bool read = GetFileSizeEx(file, &size) &&
      size.QuadPart >= static_cast<LONGLONG>(sizeof(header)) &&
      size.QuadPart <= static_cast<LONGLONG>(kMaxRawBytes) &&
      ReadFile(file, &header, sizeof(header), &received, nullptr) &&
      received == sizeof(header);
  CloseHandle(file);
  if (!read || std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 ||
      header.version != kVersion || header.header_bytes != sizeof(header) ||
      header.map_dtp_id != requested_map || header.floor != requested_floor ||
      header.pixel_format != kRgba8 || header.reserved0 || header.reserved1 ||
      !header.width || !header.height || header.width > kMaxDimension ||
      header.height > kMaxDimension || !header.compressed_bytes) {
    return false;
  }
  const uint64_t expected_raw =
      static_cast<uint64_t>(header.width) * header.height * 4ull;
  const uint64_t expected_file =
      static_cast<uint64_t>(header.header_bytes) + header.compressed_bytes;
  if (expected_raw != header.raw_bytes || expected_raw > kMaxRawBytes ||
      expected_file != static_cast<uint64_t>(size.QuadPart)) {
    return false;
  }
  *output = header;
  return true;
}

template <typename T>
void ReleaseAndNull(T*& value) {
  if (!value) return;
  value->Release();
  value = nullptr;
}

bool SameDevice(ID3D11ShaderResourceView* view, ID3D11Device* device) {
  if (!view || !device) return false;
  ID3D11Device* owner = nullptr;
  view->GetDevice(&owner);
  const bool same = owner == device;
  ReleaseAndNull(owner);
  return same;
}

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>* output) {
  if (!output) return false;
  output->clear();
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {};
  const bool size_ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
      static_cast<uint64_t>(size.QuadPart) <=
          static_cast<uint64_t>(std::numeric_limits<size_t>::max());
  if (!size_ok || size.QuadPart < static_cast<LONGLONG>(sizeof(FileHeader)) ||
      size.QuadPart > static_cast<LONGLONG>(kMaxRawBytes)) {
    CloseHandle(file);
    return false;
  }
  output->resize(static_cast<size_t>(size.QuadPart));
  size_t done = 0;
  while (done < output->size()) {
    const DWORD request = static_cast<DWORD>(std::min<size_t>(
        output->size() - done, std::numeric_limits<DWORD>::max()));
    DWORD received = 0;
    if (!ReadFile(file, output->data() + done, request, &received, nullptr) ||
        !received) {
      CloseHandle(file);
      output->clear();
      return false;
    }
    done += received;
  }
  CloseHandle(file);
  return true;
}

std::wstring DefaultGameDirectory() {
  wchar_t executable[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
  if (!length || length >= MAX_PATH) return {};
  std::wstring result(executable, length);
  const size_t slash = result.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return {};
  result.resize(slash);
  return result;
}

std::wstring RasterPath(const std::wstring& directory, uint32_t map_dtp_id,
                        int floor) {
  wchar_t name[96] = {};
  _snwprintf_s(name, _countof(name), _TRUNCATE, L"map_%u_f%d.dxm",
               static_cast<unsigned>(map_dtp_id), floor);
  std::wstring result = directory;
  if (!result.empty() && result.back() != L'\\' && result.back() != L'/') {
    result.push_back(L'\\');
  }
  result.append(name);
  return result;
}

}  // namespace

struct MapRasterCache::Impl {
  struct Cached {
    ID3D11ShaderResourceView* view = nullptr;
    ID3D11Device* device = nullptr;
    uint32_t map_dtp_id = 0;
    int floor = 0;
    uint32_t width = 0;
    uint32_t height = 0;
  };

  mutable std::mutex mutex;
  std::wstring directory;
  bool initialized = false;
  bool complete = false;
  bool logged_missing_cache = false;
  uint32_t failed_map = 0;
  int failed_floor = 0;
  uint64_t failed_report_ms = 0;
  std::unordered_map<uint64_t, MapRasterInfo> descriptions;
  std::unordered_map<uint32_t, std::vector<int>> available_floors;
  Cached current;
  Cached previous;

  ~Impl() {
    // Explicit Shutdown owns normal COM teardown. During process termination
    // d3d11.dll can already be unloaded, so intentionally do not Release here.
  }

  static void Release(Cached* cached) {
    if (!cached) return;
    ReleaseAndNull(cached->view);
    ReleaseAndNull(cached->device);
    *cached = {};
  }

  void ResetGpu() {
    Release(&current);
    Release(&previous);
  }

  static uint64_t DescriptionKey(uint32_t map_dtp_id, int floor) {
    return (static_cast<uint64_t>(map_dtp_id) << 32u) |
           static_cast<uint32_t>(floor);
  }

  bool DescribeLocked(uint32_t map_dtp_id, int floor, MapRasterInfo* output) {
    if (!output) return false;
    *output = {};
    const uint64_t key = DescriptionKey(map_dtp_id, floor);
    const auto existing = descriptions.find(key);
    if (existing != descriptions.end()) {
      *output = existing->second;
      return output->width != 0 && output->height != 0;
    }

    FileHeader header = {};
    int selected_floor = floor;
    bool exact = ReadRasterHeader(RasterPath(directory, map_dtp_id, floor),
                                  map_dtp_id, floor, &header);
    if (!exact && floor != 0) {
      selected_floor = 0;
      if (!ReadRasterHeader(RasterPath(directory, map_dtp_id, 0), map_dtp_id,
                            0, &header)) {
        return false;
      }
    } else if (!exact) {
      return false;
    }

    MapRasterInfo info;
    info.width = header.width;
    info.height = header.height;
    info.map_dtp_id = header.map_dtp_id;
    info.floor = selected_floor;
    info.exact_floor = exact;
    descriptions.emplace(key, info);
    *output = info;
    return true;
  }

  bool InitializeLocked(const wchar_t* game_directory) {
    if (initialized) return complete;
    std::wstring game = game_directory ? game_directory :
        DefaultGameDirectory();
    if (game.empty()) {
      initialized = true;
      return false;
    }
    while (!game.empty() && (game.back() == L'\\' || game.back() == L'/')) {
      game.pop_back();
    }
    directory = game + L"\\DeusExSecondScreen\\MapCache";
    const std::wstring sentinel = directory + L"\\CACHE_COMPLETE";
    complete = GetFileAttributesW(sentinel.c_str()) != INVALID_FILE_ATTRIBUTES;
    initialized = true;
    if (complete) {
      DXS_INFO("map raster cache ready: %ls", directory.c_str());
    } else if (!logged_missing_cache) {
      DXS_WARN("map raster cache unavailable: %ls", directory.c_str());
      logged_missing_cache = true;
    }
    return complete;
  }

  bool LoadAndUpload(ID3D11Device* device, const std::wstring& path,
                     uint32_t requested_map, int requested_floor,
                     Cached* output) {
    if (!device || !output) return false;
    *output = {};
    std::vector<uint8_t> file;
    if (!ReadWholeFile(path, &file)) return false;
    if (file.size() < sizeof(FileHeader)) return false;
    FileHeader header = {};
    std::memcpy(&header, file.data(), sizeof(header));
    if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 ||
        header.version != kVersion || header.header_bytes != sizeof(header) ||
        header.map_dtp_id != requested_map ||
        header.floor != requested_floor || header.pixel_format != kRgba8 ||
        header.reserved0 || header.reserved1 ||
        !header.width || !header.height ||
        header.width > kMaxDimension || header.height > kMaxDimension) {
      return false;
    }
    const uint64_t expected_raw =
        static_cast<uint64_t>(header.width) * header.height * 4ull;
    const uint64_t expected_file =
        static_cast<uint64_t>(header.header_bytes) + header.compressed_bytes;
    if (expected_raw != header.raw_bytes || expected_raw > kMaxRawBytes ||
        expected_file != file.size() || !header.compressed_bytes) {
      return false;
    }

    std::vector<uint8_t> rgba(header.raw_bytes);
    uLongf output_bytes = static_cast<uLongf>(rgba.size());
    const int z_result = uncompress(
        reinterpret_cast<Bytef*>(rgba.data()), &output_bytes,
        reinterpret_cast<const Bytef*>(file.data() + header.header_bytes),
        static_cast<uLong>(header.compressed_bytes));
    if (z_result != Z_OK || output_bytes != header.raw_bytes) return false;
    const uLong crc = crc32(0L, Z_NULL, 0);
    if (crc32(crc, reinterpret_cast<const Bytef*>(rgba.data()),
              static_cast<uInt>(rgba.size())) != header.raw_crc32) {
      return false;
    }

    D3D11_TEXTURE2D_DESC descriptor = {};
    descriptor.Width = header.width;
    descriptor.Height = header.height;
    descriptor.MipLevels = 1;
    descriptor.ArraySize = 1;
    descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    descriptor.SampleDesc.Count = 1;
    descriptor.Usage = D3D11_USAGE_IMMUTABLE;
    descriptor.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = rgba.data();
    initial.SysMemPitch = header.width * 4;
    ID3D11Texture2D* texture = nullptr;
    HRESULT result = device->CreateTexture2D(&descriptor, &initial, &texture);
    if (FAILED(result) || !texture) {
      ReleaseAndNull(texture);
      return false;
    }
    ID3D11ShaderResourceView* view = nullptr;
    result = device->CreateShaderResourceView(texture, nullptr, &view);
    ReleaseAndNull(texture);
    if (FAILED(result) || !view) {
      ReleaseAndNull(view);
      return false;
    }

    output->view = view;
    output->device = device;
    device->AddRef();
    output->map_dtp_id = header.map_dtp_id;
    output->floor = header.floor;
    output->width = header.width;
    output->height = header.height;
    return true;
  }
};

namespace {
MapRasterCache g_map_raster_cache;
}

MapRasterCache::MapRasterCache() : impl_(new (std::nothrow) Impl()) {}

MapRasterCache::~MapRasterCache() {
  delete impl_;
  impl_ = nullptr;
}

bool MapRasterCache::Initialize(const wchar_t* game_directory) {
  if (!impl_) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->InitializeLocked(game_directory);
}

bool MapRasterCache::Acquire(ID3D11Device* device, uint32_t map_dtp_id,
                             int floor, MapRasterTexture* output) {
  if (output) *output = {};
  if (!impl_ || !device || !output || !map_dtp_id || floor < -32 ||
      floor > 32) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->InitializeLocked(nullptr)) return false;

  auto matches = [device, map_dtp_id](const Impl::Cached& cached,
                                      int wanted_floor) {
    return cached.view && cached.map_dtp_id == map_dtp_id &&
        cached.floor == wanted_floor && cached.device == device &&
        SameDevice(cached.view, device);
  };
  Impl::Cached* selected = nullptr;
  if (matches(impl_->current, floor)) selected = &impl_->current;
  if (!selected && matches(impl_->previous, floor)) {
    std::swap(impl_->current, impl_->previous);
    selected = &impl_->current;
  }

  if (!selected) {
    Impl::Cached loaded;
    std::wstring path = RasterPath(impl_->directory, map_dtp_id, floor);
    const bool exact_loaded = impl_->LoadAndUpload(
        device, path, map_dtp_id, floor, &loaded);
    if (!exact_loaded && floor != 0) {
      // Do not choose a cached floor-zero fallback until the requested floor
      // has actually been tried. The previous order reused f0 immediately,
      // so changing floors could never load the authored f1/f2/... raster.
      if (matches(impl_->current, 0)) {
        selected = &impl_->current;
      } else if (matches(impl_->previous, 0)) {
        std::swap(impl_->current, impl_->previous);
        selected = &impl_->current;
      } else {
        path = RasterPath(impl_->directory, map_dtp_id, 0);
        impl_->LoadAndUpload(device, path, map_dtp_id, 0, &loaded);
      }
    }
    if (!selected && loaded.view) {
      Impl::Release(&impl_->previous);
      impl_->previous = impl_->current;
      impl_->current = loaded;
      selected = &impl_->current;
      DXS_INFO("map raster uploaded: map=%u floor=%d %ux%u",
               static_cast<unsigned>(selected->map_dtp_id), selected->floor,
               selected->width, selected->height);
    }
    if (!selected) {
      const uint64_t now = GetTickCount64();
      if (impl_->failed_map != map_dtp_id || impl_->failed_floor != floor ||
          impl_->failed_report_ms == 0 ||
          now - impl_->failed_report_ms >= 10000) {
        impl_->failed_map = map_dtp_id;
        impl_->failed_floor = floor;
        impl_->failed_report_ms = now;
        DXS_WARN("map raster missing or invalid: map=%u floor=%d cache=%ls",
                 static_cast<unsigned>(map_dtp_id), floor,
                 impl_->directory.c_str());
      }
      return false;
    }
  }

  impl_->failed_map = 0;
  impl_->failed_report_ms = 0;

  selected->view->AddRef();
  output->view = selected->view;
  output->width = selected->width;
  output->height = selected->height;
  output->map_dtp_id = selected->map_dtp_id;
  output->floor = selected->floor;
  output->exact_floor = selected->floor == floor;
  return true;
}

bool MapRasterCache::Describe(uint32_t map_dtp_id, int floor,
                              MapRasterInfo* output) {
  if (output) *output = {};
  if (!impl_ || !output || !map_dtp_id || floor < -32 || floor > 32) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->InitializeLocked(nullptr)) return false;
  return impl_->DescribeLocked(map_dtp_id, floor, output);
}

int MapRasterCache::AvailableFloors(uint32_t map_dtp_id, int* floors,
                                    int capacity) {
  if (!impl_ || !floors || capacity <= 0 || !map_dtp_id) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->InitializeLocked(nullptr)) return 0;

  const auto cached = impl_->available_floors.find(map_dtp_id);
  if (cached != impl_->available_floors.end()) {
    const int count = (std::min)(capacity,
        static_cast<int>(cached->second.size()));
    for (int index = 0; index < count; ++index) {
      floors[index] = cached->second[index];
    }
    return count;
  }

  wchar_t pattern_name[96] = {};
  _snwprintf_s(pattern_name, _countof(pattern_name), _TRUNCATE,
               L"map_%u_f*.dxm", static_cast<unsigned>(map_dtp_id));
  std::wstring pattern = impl_->directory;
  if (!pattern.empty() && pattern.back() != L'\\' &&
      pattern.back() != L'/') {
    pattern.push_back(L'\\');
  }
  pattern.append(pattern_name);

  std::vector<int> available;
  WIN32_FIND_DATAW entry = {};
  HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
  if (search != INVALID_HANDLE_VALUE) {
    do {
      if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      unsigned parsed_map = 0;
      int parsed_floor = 0;
      wchar_t trailing = L'\0';
      if (std::swscanf(entry.cFileName, L"map_%u_f%d.dxm%c", &parsed_map,
                       &parsed_floor, &trailing) != 2 ||
          parsed_map != map_dtp_id || parsed_floor < -32 ||
          parsed_floor > 32) {
        continue;
      }
      FileHeader header = {};
      if (ReadRasterHeader(RasterPath(impl_->directory, map_dtp_id,
                                      parsed_floor),
                           map_dtp_id, parsed_floor, &header)) {
        available.push_back(parsed_floor);
      }
    } while (FindNextFileW(search, &entry));
    FindClose(search);
  }
  std::sort(available.begin(), available.end());
  available.erase(std::unique(available.begin(), available.end()),
                  available.end());
  impl_->available_floors.emplace(map_dtp_id, available);
  const int count = (std::min)(capacity,
      static_cast<int>(available.size()));
  for (int index = 0; index < count; ++index) floors[index] = available[index];
  return count;
}

void MapRasterCache::Shutdown() {
  if (!impl_) return;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->ResetGpu();
  impl_->initialized = false;
  impl_->complete = false;
  impl_->directory.clear();
  impl_->descriptions.clear();
  impl_->available_floors.clear();
}

bool MapRasterCache::ready() const {
  if (!impl_) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->complete;
}

const wchar_t* MapRasterCache::directory() const {
  if (!impl_) return L"";
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->directory.c_str();
}

MapRasterCache& TheMapRasterCache() {
  return g_map_raster_cache;
}

}  // namespace dxs

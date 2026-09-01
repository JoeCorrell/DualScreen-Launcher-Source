#include "MapAtlas.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Log.h"

namespace dis {
namespace {

MapAtlas g_map_atlas;
int g_module_anchor = 0;

constexpr std::array<const char*, 5> kAreaNames = {
    "act1a", "act2a", "act3a", "act3b", "act3c"};
constexpr wchar_t kPackRelativePath[] =
    L"DI\\Data\\common_textures_0_pc.rpack";

constexpr uint32_t kTileWidth = 256;
constexpr uint32_t kTileHeight = 256;
constexpr uint32_t kTileMipCount = 9;
constexpr uint32_t kTileFormatDxt1 = 17;
constexpr uint32_t kDecodedMip = 2;
constexpr uint32_t kDecodedTileSize = 64;
constexpr uint32_t kDxt1BlockBytes = 8;
constexpr uint32_t kFullMipChainBytes = 43704;
constexpr uint32_t kDecodedMipOffset = 32768 + 8192;
constexpr uint32_t kDecodedMipBytes = 2048;
constexpr size_t kAtlasBytes =
    static_cast<size_t>(MapAtlas::kWidth) * MapAtlas::kHeight * 4;

// These are format-sanity limits, not guesses at the shipped pack. They stop
// a damaged header from turning a 3.2 GiB resource file into an unbounded table
// allocation before any offsets have been validated.
constexpr uint32_t kMaxSections = 4096;
constexpr uint32_t kMaxParts = 1000000;
constexpr uint32_t kMaxFiles = 250000;
constexpr uint32_t kMaxNamesBytes = 32 * 1024 * 1024;
constexpr uint64_t kMaxTableBytes = 128ull * 1024 * 1024;

#pragma pack(push, 1)
struct PackHeader {
  char signature[4];
  uint32_t version;
  uint32_t compression;
  uint32_t part_count;
  uint32_t section_count;
  uint32_t file_count;
  uint32_t names_size;
  uint32_t names_count;
  uint32_t block_size;
};

struct PackSection {
  uint8_t file_type;
  uint8_t unknown_a;
  uint8_t unknown_b;
  uint8_t unknown_c;
  uint32_t offset;
  uint32_t unpacked_size;
  uint32_t packed_size;
  uint32_t unknown;
};

struct PackPart {
  uint8_t section;
  uint8_t unknown_a;
  uint16_t file;
  uint32_t offset;
  uint32_t size;
  uint32_t unknown_b;
};

struct PackFile {
  uint8_t part_count;
  uint8_t unknown_a;
  uint8_t file_type;
  uint8_t unknown_b;
  uint32_t file_index;
  uint32_t first_part;
};
#pragma pack(pop)

static_assert(sizeof(PackHeader) == 36, "RP6L header layout changed");
static_assert(sizeof(PackSection) == 20, "RP6L section layout changed");
static_assert(sizeof(PackPart) == 16, "RP6L part layout changed");
static_assert(sizeof(PackFile) == 12, "RP6L file layout changed");

bool AddWouldOverflow(uint64_t a, uint64_t b) {
  return a > UINT64_MAX - b;
}

bool MultiplyWouldOverflow(uint64_t a, uint64_t b) {
  return a != 0 && b > UINT64_MAX / a;
}

bool ReadAt(HANDLE file, uint64_t file_size, uint64_t offset, void* output,
            size_t bytes) {
  if (file == INVALID_HANDLE_VALUE || (!output && bytes != 0) ||
      AddWouldOverflow(offset, bytes) || offset + bytes > file_size) {
    return false;
  }
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<LONGLONG>(offset);
  if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) return false;

  uint8_t* destination = static_cast<uint8_t*>(output);
  size_t remaining = bytes;
  while (remaining != 0) {
    const DWORD request = static_cast<DWORD>(
        std::min<size_t>(remaining, static_cast<size_t>(0x40000000u)));
    DWORD read = 0;
    if (!ReadFile(file, destination, request, &read, nullptr) ||
        read != request) {
      return false;
    }
    destination += read;
    remaining -= read;
  }
  return true;
}

bool FileSize(HANDLE file, uint64_t* size) {
  if (!size || file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER value{};
  if (!GetFileSizeEx(file, &value) || value.QuadPart <= 0) return false;
  *size = static_cast<uint64_t>(value.QuadPart);
  return true;
}

std::wstring DirectoryOf(const wchar_t* path) {
  if (!path || !*path) return {};
  std::wstring directory(path);
  const size_t slash = directory.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return {};
  directory.resize(slash + 1);
  return directory;
}

bool ExistingPackBeside(HMODULE module, std::wstring* result) {
  if (!result) return false;
  std::array<wchar_t, 4096> path{};
  const DWORD length = GetModuleFileNameW(module, path.data(),
                                           static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size() - 1) return false;
  std::wstring candidate = DirectoryOf(path.data());
  if (candidate.empty()) return false;
  candidate += kPackRelativePath;
  const DWORD attributes = GetFileAttributesW(candidate.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return false;
  }
  *result = std::move(candidate);
  return true;
}

bool FindPack(std::wstring* result) {
  // The proxy and executable normally share a directory. Prefer the executable
  // because WEMU may load the DLL through a translated or canonicalized path.
  if (ExistingPackBeside(nullptr, result)) return true;

  HMODULE self = nullptr;
  if (GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(&g_module_anchor), &self) &&
      ExistingPackBeside(self, result)) {
    return true;
  }
  return false;
}

bool SectionRange(const PackSection& section, const PackPart& part,
                  uint64_t multiplier, uint64_t file_size,
                  uint64_t* absolute) {
  if (!absolute || section.packed_size != 0) return false;
  // A valid uncompressed part must be contained in the section's logical
  // extent as well as the physical file. This rejects wrapping offsets before
  // the absolute address is formed.
  if (AddWouldOverflow(part.offset, part.size) ||
      static_cast<uint64_t>(part.offset) + part.size >
          section.unpacked_size) {
    return false;
  }
  if (MultiplyWouldOverflow(section.offset, multiplier) ||
      MultiplyWouldOverflow(part.offset, multiplier)) {
    return false;
  }
  const uint64_t section_offset =
      static_cast<uint64_t>(section.offset) * multiplier;
  const uint64_t part_offset = static_cast<uint64_t>(part.offset) * multiplier;
  if (AddWouldOverflow(section_offset, part_offset)) return false;
  const uint64_t position = section_offset + part_offset;
  if (AddWouldOverflow(position, part.size) ||
      position + part.size > file_size) {
    return false;
  }
  *absolute = position;
  return true;
}

int ParseMapTileName(const char* name, size_t length, int* tile) {
  if (!name || !tile || length < 8) return -1;
  for (size_t area = 0; area < kAreaNames.size(); ++area) {
    const char* prefix = kAreaNames[area];
    constexpr size_t kPrefixLength = 5;
    if (length <= kPrefixLength + 5 ||
        _strnicmp(name, prefix, kPrefixLength) != 0 ||
        _strnicmp(name + kPrefixLength, "_map_", 5) != 0) {
      continue;
    }
    unsigned value = 0;
    const size_t first_digit = kPrefixLength + 5;
    if (first_digit >= length) return -1;
    for (size_t i = first_digit; i < length; ++i) {
      if (name[i] < '0' || name[i] > '9') return -1;
      value = value * 10u + static_cast<unsigned>(name[i] - '0');
      if (value > 256u) return -1;
    }
    if (value == 0) return -1;
    *tile = static_cast<int>(value - 1);
    return static_cast<int>(area);
  }
  return -1;
}

uint8_t Expand5(uint16_t value) {
  return static_cast<uint8_t>((value * 255u + 15u) / 31u);
}

uint8_t Expand6(uint16_t value) {
  return static_cast<uint8_t>((value * 255u + 31u) / 63u);
}

void Decode565(uint16_t value, uint8_t* rgba) {
  rgba[0] = Expand5(static_cast<uint16_t>((value >> 11) & 31));
  rgba[1] = Expand6(static_cast<uint16_t>((value >> 5) & 63));
  rgba[2] = Expand5(static_cast<uint16_t>(value & 31));
  rgba[3] = 255;
}

bool DecodeDxt1Tile(const uint8_t* blocks, size_t block_bytes,
                    int tile_x, int tile_y, uint8_t* atlas,
                    size_t atlas_bytes) {
  if (!blocks || !atlas || block_bytes != kDecodedMipBytes ||
      atlas_bytes != kAtlasBytes || tile_x < 0 || tile_x >= 16 ||
      tile_y < 0 || tile_y >= 16) {
    return false;
  }

  constexpr int kBlocksAcross = kDecodedTileSize / 4;
  for (int block_y = 0; block_y < kBlocksAcross; ++block_y) {
    for (int block_x = 0; block_x < kBlocksAcross; ++block_x) {
      const uint8_t* block =
          blocks + static_cast<size_t>(block_y * kBlocksAcross + block_x) *
                       kDxt1BlockBytes;
      const uint16_t c0 = static_cast<uint16_t>(block[0]) |
                          (static_cast<uint16_t>(block[1]) << 8);
      const uint16_t c1 = static_cast<uint16_t>(block[2]) |
                          (static_cast<uint16_t>(block[3]) << 8);
      uint8_t colors[4][4]{};
      Decode565(c0, colors[0]);
      Decode565(c1, colors[1]);
      if (c0 > c1) {
        for (int channel = 0; channel < 3; ++channel) {
          colors[2][channel] = static_cast<uint8_t>(
              (2u * colors[0][channel] + colors[1][channel]) / 3u);
          colors[3][channel] = static_cast<uint8_t>(
              (colors[0][channel] + 2u * colors[1][channel]) / 3u);
        }
        colors[2][3] = colors[3][3] = 255;
      } else {
        for (int channel = 0; channel < 3; ++channel) {
          colors[2][channel] = static_cast<uint8_t>(
              (colors[0][channel] + colors[1][channel]) / 2u);
        }
        colors[2][3] = 255;
        colors[3][0] = colors[3][1] = colors[3][2] = colors[3][3] = 0;
      }

      const uint32_t indices = static_cast<uint32_t>(block[4]) |
                               (static_cast<uint32_t>(block[5]) << 8) |
                               (static_cast<uint32_t>(block[6]) << 16) |
                               (static_cast<uint32_t>(block[7]) << 24);
      for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
          const unsigned selector =
              (indices >> (2u * static_cast<unsigned>(py * 4 + px))) & 3u;
          const int x = tile_x * static_cast<int>(kDecodedTileSize) +
                        block_x * 4 + px;
          const int y = tile_y * static_cast<int>(kDecodedTileSize) +
                        block_y * 4 + py;
          uint8_t* output = atlas +
              (static_cast<size_t>(y) * MapAtlas::kWidth + x) * 4;
          std::memcpy(output, colors[selector], 4);
        }
      }
    }
  }
  return true;
}

std::string LowerAscii(const std::string& text) {
  std::string lower = text;
  for (char& c : lower) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return lower;
}

}  // namespace

MapAtlas& TheMapAtlas() { return g_map_atlas; }

int MapAtlas::AreaIndex(const std::string& level_name) {
  const std::string lower = LowerAscii(level_name);
  for (size_t i = 0; i < kAreaNames.size(); ++i) {
    if (lower.find(kAreaNames[i]) != std::string::npos) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool MapAtlas::SupportsLevel(const std::string& level_name) const {
  return AreaIndex(level_name) >= 0;
}

bool MapAtlas::EnsureLoaded(const std::string& level_name) {
  const int area = AreaIndex(level_name);
  if (area < 0) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  return EnsureLoadedLocked(area);
}

bool MapAtlas::EnsureLoadedLocked(int area) {
  if (area < 0 || area >= static_cast<int>(areas_.size())) return false;
  AreaCache& cache = areas_[static_cast<size_t>(area)];
  if (cache.state == LoadState::kReady) return true;
  if (cache.state == LoadState::kFailed) return false;
  if (!BuildIndexLocked()) {
    FailAreaLocked(area, index_error_.c_str());
    return false;
  }
  return LoadAreaLocked(area);
}

void MapAtlas::FailAreaLocked(int area, const char* reason) {
  if (area < 0 || area >= static_cast<int>(areas_.size())) return;
  AreaCache& cache = areas_[static_cast<size_t>(area)];
  cache.rgba.clear();
  cache.state = LoadState::kFailed;
  cache.error = (reason && *reason) ? reason : "Map artwork could not be loaded";
  DIS_WARN("[map-atlas] %s: %s", kAreaNames[static_cast<size_t>(area)],
           cache.error.c_str());
}

bool MapAtlas::BuildIndexLocked() {
  if (index_attempted_) return index_ready_;
  index_attempted_ = true;

  if (!FindPack(&pack_path_)) {
    index_error_ = "DI/Data/common_textures_0_pc.rpack was not found";
    return false;
  }

  HANDLE file = CreateFileW(pack_path_.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    index_error_ = "The map resource pack could not be opened";
    return false;
  }

  uint64_t file_size = 0;
  PackHeader header{};
  if (!FileSize(file, &file_size) ||
      !ReadAt(file, file_size, 0, &header, sizeof(header)) ||
      std::memcmp(header.signature, "RP6L", 4) != 0) {
    CloseHandle(file);
    index_error_ = "The map resource pack has an invalid RP6L header";
    return false;
  }
  if ((header.version != 1 && header.version != 4) ||
      header.section_count == 0 || header.section_count > kMaxSections ||
      header.part_count == 0 || header.part_count > kMaxParts ||
      header.file_count == 0 || header.file_count > kMaxFiles ||
      header.names_size == 0 || header.names_size > kMaxNamesBytes ||
      header.names_count != header.file_count) {
    CloseHandle(file);
    index_error_ = "The map resource pack table counts are invalid";
    return false;
  }

  const uint64_t section_bytes =
      static_cast<uint64_t>(header.section_count) * sizeof(PackSection);
  const uint64_t part_bytes =
      static_cast<uint64_t>(header.part_count) * sizeof(PackPart);
  const uint64_t file_bytes =
      static_cast<uint64_t>(header.file_count) * sizeof(PackFile);
  const uint64_t offset_bytes =
      static_cast<uint64_t>(header.file_count) * sizeof(uint32_t);
  const uint64_t table_bytes = sizeof(PackHeader) + section_bytes + part_bytes +
                               file_bytes + offset_bytes + header.names_size;
  if (table_bytes > kMaxTableBytes || table_bytes > file_size) {
    CloseHandle(file);
    index_error_ = "The map resource pack tables are truncated";
    return false;
  }

  std::vector<PackSection> sections(header.section_count);
  std::vector<PackPart> parts(header.part_count);
  std::vector<PackFile> files(header.file_count);
  std::vector<uint32_t> name_offsets(header.file_count);
  std::vector<char> names(static_cast<size_t>(header.names_size) + 1, '\0');

  uint64_t cursor = sizeof(PackHeader);
  bool tables_ok = ReadAt(file, file_size, cursor, sections.data(),
                          static_cast<size_t>(section_bytes));
  cursor += section_bytes;
  tables_ok = tables_ok &&
      ReadAt(file, file_size, cursor, parts.data(),
             static_cast<size_t>(part_bytes));
  cursor += part_bytes;
  tables_ok = tables_ok &&
      ReadAt(file, file_size, cursor, files.data(),
             static_cast<size_t>(file_bytes));
  cursor += file_bytes;
  tables_ok = tables_ok &&
      ReadAt(file, file_size, cursor, name_offsets.data(),
             static_cast<size_t>(offset_bytes));
  cursor += offset_bytes;
  tables_ok = tables_ok &&
      ReadAt(file, file_size, cursor, names.data(), header.names_size);
  if (!tables_ok) {
    CloseHandle(file);
    index_error_ = "The map resource pack tables could not be read";
    return false;
  }

  const uint64_t multiplier = header.version == 4 ? 16u : 1u;
  bool malformed_map_entry = false;
  for (uint32_t file_index = 0; file_index < header.file_count; ++file_index) {
    const uint32_t name_offset = name_offsets[file_index];
    if (name_offset >= header.names_size) {
      malformed_map_entry = true;
      break;
    }
    const char* name = names.data() + name_offset;
    const size_t remaining = header.names_size - name_offset;
    const void* terminator = std::memchr(name, '\0', remaining);
    if (!terminator) {
      malformed_map_entry = true;
      break;
    }
    const size_t length =
        static_cast<const char*>(terminator) - name;
    int tile_index = -1;
    const int area = ParseMapTileName(name, length, &tile_index);
    if (area < 0) continue;

    const PackFile& entry = files[file_index];
    if (entry.file_type != 0x20 || entry.part_count == 0 ||
        entry.first_part >= header.part_count ||
        static_cast<uint64_t>(entry.first_part) + entry.part_count >
            header.part_count) {
      malformed_map_entry = true;
      break;
    }

    TileRef candidate{};
    for (uint32_t p = entry.first_part;
         p < entry.first_part + entry.part_count; ++p) {
      const PackPart& part = parts[p];
      if (part.size != kFullMipChainBytes ||
          part.section >= header.section_count) {
        continue;
      }
      uint64_t absolute = 0;
      if (!SectionRange(sections[part.section], part, multiplier, file_size,
                        &absolute)) {
        malformed_map_entry = true;
        break;
      }
      if (candidate.valid) {
        malformed_map_entry = true;
        break;
      }
      candidate.data_offset = absolute;
      candidate.data_size = part.size;
      candidate.valid = true;
    }
    if (malformed_map_entry || !candidate.valid) {
      malformed_map_entry = true;
      break;
    }

    AreaCache& cache = areas_[static_cast<size_t>(area)];
    TileRef& destination = cache.tiles[static_cast<size_t>(tile_index)];
    if (destination.valid) {
      malformed_map_entry = true;
      break;
    }
    destination = candidate;
    ++cache.tile_count;
  }
  CloseHandle(file);

  if (malformed_map_entry) {
    index_error_ = "The map resource pack contains a malformed map entry";
    return false;
  }
  for (const AreaCache& cache : areas_) {
    if (cache.tile_count != 256) {
      index_error_ = "The map resource pack is missing one or more map tiles";
      return false;
    }
  }

  // The constants above describe the exact shipped resource. Keep the check
  // next to the index validation so a future edit cannot silently read a
  // different mip or format.
  uint32_t expected_chain = 0;
  for (uint32_t mip = 0; mip < kTileMipCount; ++mip) {
    const uint32_t width = std::max(1u, kTileWidth >> mip);
    const uint32_t height = std::max(1u, kTileHeight >> mip);
    expected_chain += ((width + 3u) / 4u) * ((height + 3u) / 4u) *
                      kDxt1BlockBytes;
  }
  if (expected_chain != kFullMipChainBytes || kTileFormatDxt1 != 17 ||
      kDecodedMip != 2) {
    index_error_ = "The built-in map texture layout is inconsistent";
    return false;
  }

  index_ready_ = true;
  DIS_INFO("[map-atlas] indexed 5 x 256 native map tiles from common_textures_0_pc.rpack");
  return true;
}

bool MapAtlas::LoadAreaLocked(int area) {
  AreaCache& cache = areas_[static_cast<size_t>(area)];
  if (cache.tile_count != 256) {
    FailAreaLocked(area, "The selected area does not contain all 256 tiles");
    return false;
  }

  HANDLE file = CreateFileW(pack_path_.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  uint64_t file_size = 0;
  if (file == INVALID_HANDLE_VALUE || !FileSize(file, &file_size)) {
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    FailAreaLocked(area, "The map resource pack could not be reopened");
    return false;
  }

  const DWORD started = GetTickCount();
  cache.rgba.assign(kAtlasBytes, 0);
  std::array<uint8_t, kDecodedMipBytes> compressed{};
  for (size_t tile = 0; tile < cache.tiles.size(); ++tile) {
    const TileRef& reference = cache.tiles[tile];
    if (!reference.valid || reference.data_size != kFullMipChainBytes ||
        AddWouldOverflow(reference.data_offset, kDecodedMipOffset) ||
        !ReadAt(file, file_size,
                reference.data_offset + kDecodedMipOffset,
                compressed.data(), compressed.size()) ||
        !DecodeDxt1Tile(compressed.data(), compressed.size(),
                        static_cast<int>(tile % 16),
                        static_cast<int>(tile / 16), cache.rgba.data(),
                        cache.rgba.size())) {
      CloseHandle(file);
      FailAreaLocked(area, "A native map tile is truncated or invalid");
      return false;
    }
  }
  CloseHandle(file);

  cache.state = LoadState::kReady;
  cache.error.clear();
  DIS_INFO("[map-atlas] decoded %s native atlas (1024x1024, 512 KiB read) in %lu ms",
           kAreaNames[static_cast<size_t>(area)],
           static_cast<unsigned long>(GetTickCount() - started));
  return true;
}

bool MapAtlas::Draw(Canvas& canvas, const Box& destination,
                    const std::string& level_name, const Color& tint) {
  if (destination.w <= 0 || destination.h <= 0) return false;
  const int area = AreaIndex(level_name);
  if (area < 0) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  if (!EnsureLoadedLocked(area)) return false;
  const AreaCache& cache = areas_[static_cast<size_t>(area)];
  const int side = std::min(destination.w, destination.h);
  const Box fitted(destination.x + (destination.w - side) / 2,
                   destination.y + (destination.h - side) / 2, side, side);
  canvas.BlitRgba(fitted, cache.rgba.data(), kWidth, kHeight,
                  Box(0, 0, kWidth, kHeight), tint);
  return true;
}

bool MapAtlas::DrawViewport(Canvas& canvas, const Box& destination,
                            const std::string& level_name, float center_u,
                            float center_v, float zoom, const Color& tint) {
  if (destination.w <= 0 || destination.h <= 0 || !std::isfinite(center_u) ||
      !std::isfinite(center_v) || !std::isfinite(zoom)) {
    return false;
  }
  const int area = AreaIndex(level_name);
  if (area < 0) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  if (!EnsureLoadedLocked(area)) return false;
  const AreaCache& cache = areas_[static_cast<size_t>(area)];

  zoom = std::max(1.0f, std::min(16.0f, zoom));
  const float aspect = static_cast<float>(destination.w) / destination.h;
  float source_w = static_cast<float>(kWidth);
  float source_h = static_cast<float>(kHeight);
  if (aspect >= 1.0f) {
    source_h = source_w / aspect;
  } else {
    source_w = source_h * aspect;
  }
  source_w /= zoom;
  source_h /= zoom;
  const int width = std::max(1, std::min(kWidth,
      static_cast<int>(std::lround(source_w))));
  const int height = std::max(1, std::min(kHeight,
      static_cast<int>(std::lround(source_h))));
  center_u = std::max(0.0f, std::min(1.0f, center_u));
  center_v = std::max(0.0f, std::min(1.0f, center_v));
  const int x = std::max(0, std::min(kWidth - width,
      static_cast<int>(std::lround(center_u * kWidth - width * 0.5f))));
  const int y = std::max(0, std::min(kHeight - height,
      static_cast<int>(std::lround(center_v * kHeight - height * 0.5f))));
  canvas.BlitRgba(destination, cache.rgba.data(), kWidth, kHeight,
                  Box(x, y, width, height), tint);
  return true;
}

bool MapAtlas::DrawViewportIfReady(Canvas& canvas, const Box& destination,
                                   const std::string& level_name,
                                   float center_u, float center_v, float zoom,
                                   const Color& tint) {
  if (destination.w <= 0 || destination.h <= 0 || !std::isfinite(center_u) ||
      !std::isfinite(center_v) || !std::isfinite(zoom)) {
    return false;
  }
  const int area = AreaIndex(level_name);
  if (area < 0) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  const AreaCache& cache = areas_[static_cast<size_t>(area)];
  if (cache.state != LoadState::kReady ||
      cache.rgba.size() < static_cast<size_t>(kWidth) * kHeight * 4) {
    return false;
  }

  zoom = std::max(1.0f, std::min(16.0f, zoom));
  const float aspect = static_cast<float>(destination.w) / destination.h;
  float source_w = static_cast<float>(kWidth);
  float source_h = static_cast<float>(kHeight);
  if (aspect >= 1.0f) source_h = source_w / aspect;
  else source_w = source_h * aspect;
  source_w /= zoom;
  source_h /= zoom;
  const int width = std::max(1, std::min(kWidth,
      static_cast<int>(std::lround(source_w))));
  const int height = std::max(1, std::min(kHeight,
      static_cast<int>(std::lround(source_h))));
  center_u = std::max(0.0f, std::min(1.0f, center_u));
  center_v = std::max(0.0f, std::min(1.0f, center_v));
  const int x = std::max(0, std::min(kWidth - width,
      static_cast<int>(std::lround(center_u * kWidth - width * 0.5f))));
  const int y = std::max(0, std::min(kHeight - height,
      static_cast<int>(std::lround(center_v * kHeight - height * 0.5f))));
  canvas.BlitRgba(destination, cache.rgba.data(), kWidth, kHeight,
                  Box(x, y, width, height), tint);
  return true;
}

std::string MapAtlas::LastError(const std::string& level_name) const {
  const int area = AreaIndex(level_name);
  if (area < 0) return "No native atlas exists for this level";
  std::lock_guard<std::mutex> lock(mutex_);
  const AreaCache& cache = areas_[static_cast<size_t>(area)];
  if (!cache.error.empty()) return cache.error;
  if (!index_error_.empty()) return index_error_;
  return cache.state == LoadState::kReady ? std::string() :
      "Native map artwork has not been loaded yet";
}

}  // namespace dis

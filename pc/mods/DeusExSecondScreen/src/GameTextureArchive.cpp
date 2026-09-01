#include "GameTextureArchive.h"

#include <d3d11.h>
#include <windows.h>
#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Log.h"

namespace dxs {
namespace {

constexpr uint32_t kBigFileAlignment = 0x7ff00000u;
constexpr uint32_t kAlternateBigFileAlignment = 0x62300000u;
constexpr uint32_t kSectorSize = 2048u;
constexpr uint32_t kDefaultLocale = 0xffffffffu;
constexpr uint32_t kMaximumArchiveEntries = 1000000u;
constexpr uint32_t kMaximumDrmSections = 100000u;
constexpr size_t kMaximumCompressedDrmBytes = 128u * 1024u * 1024u;
constexpr size_t kMaximumDecompressedDrmBytes = 512u * 1024u * 1024u;
constexpr uint8_t kRenderResourceSectionType = 5;
constexpr uint32_t kPcd9Magic = 0x39444350u;
constexpr uint32_t kPcd9Dxt1 = 0x31545844u;
constexpr uint32_t kPcd9Dxt5 = 0x35545844u;
constexpr char kPickupDatabasePath[] = "pc-w\\pickup_database.drm";
constexpr char kGlobalScaleformDatabasePath[] =
    "pc-w\\globalscaleformdatabase.drm";
constexpr char kGlobalPlayerInfoPath[] = "pc-w\\globalplayerinfo.drm";

template <typename T>
void ReleaseAndNull(T*& object) {
  if (object) object->Release();
  object = nullptr;
}

bool AddWouldOverflow(size_t left, size_t right) {
  return right > static_cast<size_t>(-1) - left;
}

bool Align16(size_t value, size_t* output) {
  if (!output || value > static_cast<size_t>(-1) - 15u) return false;
  *output = (value + 15u) & ~size_t{15u};
  return true;
}

uint16_t ReadU16Le(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8u);
}

uint32_t ReadU32Le(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8u) |
         (static_cast<uint32_t>(data[2]) << 16u) |
         (static_cast<uint32_t>(data[3]) << 24u);
}

uint32_t HashFileName(const char* path) {
  uint32_t hash = 0xffffffffu;
  if (!path) return 0;
  for (const unsigned char* cursor =
           reinterpret_cast<const unsigned char*>(path);
       *cursor; ++cursor) {
    hash ^= static_cast<uint32_t>(*cursor) << 24u;
    for (int bit = 0; bit < 8; ++bit) {
      hash = (hash & 0x80000000u) ?
                 ((hash << 1u) ^ 0x04c11db7u) :
                 (hash << 1u);
    }
  }
  return ~hash;
}

std::wstring ExecutableDirectory() {
  std::vector<wchar_t> buffer(1024u);
  for (;;) {
    const DWORD copied = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (copied == 0) return {};
    if (copied < buffer.size() - 1u) {
      std::wstring path(buffer.data(), copied);
      const size_t slash = path.find_last_of(L"\\/");
      if (slash == std::wstring::npos) return {};
      path.resize(slash);
      return path;
    }
    if (buffer.size() >= 32768u) return {};
    buffer.resize(std::min<size_t>(buffer.size() * 2u, 32768u));
  }
}

std::wstring JoinPath(const std::wstring& directory, const wchar_t* name) {
  if (directory.empty() || !name || !*name) return {};
  std::wstring result = directory;
  const wchar_t tail = result.back();
  if (tail != L'\\' && tail != L'/') result.push_back(L'\\');
  result.append(name);
  return result;
}

bool ReadFileRange(const std::wstring& path, uint64_t offset, size_t size,
                   std::vector<uint8_t>* output) {
  if (!output) return false;
  output->clear();
  if (size == 0) return true;
  if (size > static_cast<size_t>(0xffffffffu)) return false;

  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;

  LARGE_INTEGER file_size = {};
  LARGE_INTEGER position = {};
  position.QuadPart = static_cast<LONGLONG>(offset);
  const bool valid_range =
      GetFileSizeEx(file, &file_size) != FALSE &&
      offset <= static_cast<uint64_t>(file_size.QuadPart) &&
      size <= static_cast<uint64_t>(file_size.QuadPart) - offset &&
      SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE;
  if (!valid_range) {
    CloseHandle(file);
    return false;
  }

  output->resize(size);
  size_t completed = 0;
  while (completed < size) {
    const DWORD request = static_cast<DWORD>(
        std::min<size_t>(size - completed, 16u * 1024u * 1024u));
    DWORD received = 0;
    if (!ReadFile(file, output->data() + completed, request, &received, nullptr) ||
        received == 0) {
      output->clear();
      CloseHandle(file);
      return false;
    }
    completed += received;
  }
  CloseHandle(file);
  return true;
}

struct BigFileEntry {
  uint32_t size = 0;
  uint32_t block_offset = 0;
  uint32_t locale = 0;
  uint32_t compressed_size = 0;
};

bool LocateBigFileEntry(const std::wstring& index_path, uint32_t wanted_hash,
                        uint32_t* data_alignment, BigFileEntry* output) {
  if (!data_alignment || !output) return false;

  std::vector<uint8_t> prefix;
  if (!ReadFileRange(index_path, 0, 72u, &prefix) || prefix.size() != 72u) {
    return false;
  }
  const uint32_t alignment = ReadU32Le(prefix.data());
  if (alignment != kBigFileAlignment &&
      alignment != kAlternateBigFileAlignment) {
    return false;
  }
  const uint32_t count = ReadU32Le(prefix.data() + 68u);
  if (count == 0 || count > kMaximumArchiveEntries) return false;

  const size_t hashes_size = static_cast<size_t>(count) * sizeof(uint32_t);
  std::vector<uint8_t> hashes;
  if (!ReadFileRange(index_path, 72u, hashes_size, &hashes)) return false;

  bool found = false;
  BigFileEntry candidate;
  for (uint32_t index = 0; index < count; ++index) {
    if (ReadU32Le(hashes.data() + static_cast<size_t>(index) * 4u) !=
        wanted_hash) {
      continue;
    }

    const uint64_t record_offset = 72ull + hashes_size +
                                   static_cast<uint64_t>(index) * 16ull;
    std::vector<uint8_t> record;
    if (!ReadFileRange(index_path, record_offset, 16u, &record)) continue;
    BigFileEntry entry;
    entry.size = ReadU32Le(record.data());
    entry.block_offset = ReadU32Le(record.data() + 4u);
    entry.locale = ReadU32Le(record.data() + 8u);
    entry.compressed_size = ReadU32Le(record.data() + 12u);
    if (entry.size == 0 || entry.compressed_size != 0) continue;

    candidate = entry;
    found = true;
    if (entry.locale == kDefaultLocale) break;
  }
  if (!found) return false;
  *data_alignment = alignment;
  *output = candidate;
  return true;
}

std::wstring BigFilePartPath(const std::wstring& index_path, uint32_t part) {
  if (part > 999u || index_path.size() < 3u) return {};
  std::wstring path = index_path;
  wchar_t digits[4] = {};
  std::swprintf(digits, sizeof(digits) / sizeof(digits[0]), L"%03u", part);
  path.replace(path.size() - 3u, 3u, digits);
  return path;
}

bool ReadArchivedDrm(const std::wstring& game_directory,
                     const char* virtual_path,
                     std::vector<uint8_t>* compressed_drm) {
  if (!virtual_path || !*virtual_path || !compressed_drm) return false;
  const std::wstring index_path = JoinPath(game_directory, L"BIGFILE.000");
  const uint32_t hash = HashFileName(virtual_path);
  uint32_t alignment = 0;
  BigFileEntry entry;
  if (!LocateBigFileEntry(index_path, hash, &alignment, &entry)) return false;
  if (entry.size > kMaximumCompressedDrmBytes || alignment < kSectorSize) {
    return false;
  }

  const uint32_t blocks_per_part = alignment / kSectorSize;
  if (blocks_per_part == 0) return false;
  const uint32_t part = entry.block_offset / blocks_per_part;
  const uint64_t local_block = entry.block_offset % blocks_per_part;
  const uint64_t byte_offset = local_block * kSectorSize;
  const std::wstring data_path = BigFilePartPath(index_path, part);
  return !data_path.empty() &&
         ReadFileRange(data_path, byte_offset, entry.size, compressed_drm);
}

struct CdrmBlock {
  uint8_t type = 0;
  uint32_t compressed_size = 0;
  uint32_t uncompressed_size = 0;
};

bool DecompressCdrm(const std::vector<uint8_t>& input,
                    std::vector<uint8_t>* output) {
  if (!output) return false;
  output->clear();
  if (input.size() < 16u || std::memcmp(input.data(), "CDRM", 4u) != 0) {
    return false;
  }

  // Director's Cut PC archives use little-endian CDRM v2.  Rejecting other
  // wrappers is intentional: guessing byte order here can turn corrupt archive
  // metadata into very large allocations inside the game process.
  if (ReadU32Le(input.data() + 4u) != 2u) return false;
  const uint32_t count = ReadU32Le(input.data() + 8u);
  const uint32_t padding = ReadU32Le(input.data() + 12u);
  if (count == 0 || count > 100000u) return false;
  const size_t descriptors_size = static_cast<size_t>(count) * 8u;
  if (AddWouldOverflow(16u, descriptors_size) ||
      16u + descriptors_size > input.size() ||
      AddWouldOverflow(16u + descriptors_size, padding)) {
    return false;
  }
  size_t input_offset = 16u + descriptors_size + padding;
  if (input_offset > input.size()) return false;

  std::vector<CdrmBlock> blocks;
  blocks.reserve(count);
  size_t total_output = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* descriptor = input.data() + 16u + static_cast<size_t>(i) * 8u;
    const uint32_t flags = ReadU32Le(descriptor);
    CdrmBlock block;
    block.type = static_cast<uint8_t>(flags & 0xffu);
    block.uncompressed_size = (flags >> 8u) & 0x00ffffffu;
    block.compressed_size = ReadU32Le(descriptor + 4u);
    if ((block.type != 1u && block.type != 2u) ||
        block.uncompressed_size == 0u || block.compressed_size == 0u) {
      return false;
    }
    size_t aligned_output = 0;
    if (!Align16(block.uncompressed_size, &aligned_output) ||
        AddWouldOverflow(total_output, aligned_output)) {
      return false;
    }
    total_output += aligned_output;
    if (total_output > kMaximumDecompressedDrmBytes) return false;
    blocks.push_back(block);
  }

  output->assign(total_output, 0u);
  size_t output_offset = 0;
  for (const CdrmBlock& block : blocks) {
    size_t aligned_input = 0;
    if (!Align16(block.compressed_size, &aligned_input) ||
        input_offset > input.size() ||
        block.compressed_size > input.size() - input_offset) {
      output->clear();
      return false;
    }

    uint8_t* destination = output->data() + output_offset;
    const uint8_t* source = input.data() + input_offset;
    if (block.type == 1u) {
      if (block.compressed_size != block.uncompressed_size) {
        output->clear();
        return false;
      }
      std::memcpy(destination, source, block.uncompressed_size);
    } else {
      uLongf destination_size = block.uncompressed_size;
      const int result = uncompress(
          reinterpret_cast<Bytef*>(destination), &destination_size,
          reinterpret_cast<const Bytef*>(source), block.compressed_size);
      if (result != Z_OK || destination_size != block.uncompressed_size) {
        output->clear();
        return false;
      }
    }
    size_t aligned_output = 0;
    if (!Align16(block.uncompressed_size, &aligned_output)) {
      output->clear();
      return false;
    }
    output_offset += aligned_output;
    input_offset += aligned_input;
  }
  return output_offset == output->size();
}

struct MipLevel {
  uint32_t offset = 0;
  uint32_t size = 0;
  uint32_t row_pitch = 0;
};

struct TexturePayload {
  uint32_t width = 0;
  uint32_t height = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  std::vector<uint8_t> bytes;
  std::vector<MipLevel> mips;
};

bool ParsePcd9(const uint8_t* data, size_t size, TexturePayload* output) {
  if (!data || !output || size < 28u || ReadU32Le(data) != kPcd9Magic) {
    return false;
  }
  const uint32_t format = ReadU32Le(data + 4u);
  const uint32_t data_size = ReadU32Le(data + 8u);
  const uint32_t width = ReadU16Le(data + 16u);
  const uint32_t height = ReadU16Le(data + 18u);
  const uint32_t mip_count = 1u + data[23u];
  const uint16_t flags = ReadU16Le(data + 24u);
  if (width == 0 || height == 0 || width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
      height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
      mip_count == 0 || mip_count > D3D11_REQ_MIP_LEVELS ||
      (flags & 0xc000u) != 0 || data_size > size - 28u) {
    return false;
  }

  DXGI_FORMAT dxgi_format = DXGI_FORMAT_UNKNOWN;
  uint32_t block_size = 0;
  if (format == kPcd9Dxt1) {
    dxgi_format = DXGI_FORMAT_BC1_UNORM;
    block_size = 8u;
  } else if (format == kPcd9Dxt5) {
    dxgi_format = DXGI_FORMAT_BC3_UNORM;
    block_size = 16u;
  } else {
    return false;
  }

  TexturePayload parsed;
  parsed.width = width;
  parsed.height = height;
  parsed.format = dxgi_format;
  parsed.bytes.assign(data + 28u, data + 28u + data_size);
  parsed.mips.reserve(mip_count);

  uint32_t mip_width = width;
  uint32_t mip_height = height;
  size_t data_offset = 0;
  for (uint32_t mip = 0; mip < mip_count; ++mip) {
    mip_width = std::max<uint32_t>(1u, mip_width);
    mip_height = std::max<uint32_t>(1u, mip_height);
    const uint32_t blocks_wide = (mip_width + 3u) / 4u;
    const uint32_t blocks_high = (mip_height + 3u) / 4u;
    const uint64_t row_pitch = static_cast<uint64_t>(blocks_wide) * block_size;
    const uint64_t mip_size = row_pitch * blocks_high;
    if (row_pitch > 0xffffffffu || mip_size > 0xffffffffu ||
        data_offset > parsed.bytes.size() ||
        mip_size > parsed.bytes.size() - data_offset) {
      return false;
    }
    MipLevel level;
    level.offset = static_cast<uint32_t>(data_offset);
    level.size = static_cast<uint32_t>(mip_size);
    level.row_pitch = static_cast<uint32_t>(row_pitch);
    parsed.mips.push_back(level);
    data_offset += static_cast<size_t>(mip_size);
    mip_width >>= 1u;
    mip_height >>= 1u;
  }
  if (data_offset != parsed.bytes.size()) return false;
  *output = std::move(parsed);
  return true;
}

struct DrmSectionHeader {
  uint32_t data_size = 0;
  uint8_t type = 0;
  uint8_t unknown05 = 0;
  uint32_t flags = 0;
  uint32_t id = 0;
};

bool ParseDrmTextures(const std::vector<uint8_t>& drm,
                      std::unordered_map<uint32_t, TexturePayload>* textures) {
  if (!textures) return false;
  if (drm.size() < 32u || ReadU32Le(drm.data()) != 21u) return false;

  const uint32_t unknown04_size = ReadU32Le(drm.data() + 4u);
  const uint32_t unknown08_size = ReadU32Le(drm.data() + 8u);
  const uint32_t unknown0c = ReadU32Le(drm.data() + 12u);
  const uint32_t flags = ReadU32Le(drm.data() + 20u);
  const uint32_t section_count = ReadU32Le(drm.data() + 24u);
  if (unknown0c != 0u || section_count == 0u ||
      section_count > kMaximumDrmSections) {
    return false;
  }
  const size_t headers_size = static_cast<size_t>(section_count) * 20u;
  if (headers_size > drm.size() - 32u) return false;

  std::vector<DrmSectionHeader> headers;
  headers.reserve(section_count);
  for (uint32_t index = 0; index < section_count; ++index) {
    const uint8_t* source = drm.data() + 32u + static_cast<size_t>(index) * 20u;
    DrmSectionHeader header;
    header.data_size = ReadU32Le(source);
    header.type = source[4u];
    header.unknown05 = source[5u];
    header.flags = ReadU32Le(source + 8u);
    header.id = ReadU32Le(source + 12u);
    headers.push_back(header);
  }

  size_t offset = 32u + headers_size;
  if (AddWouldOverflow(offset, unknown08_size) ||
      AddWouldOverflow(offset + unknown08_size, unknown04_size)) {
    return false;
  }
  offset += unknown08_size;
  offset += unknown04_size;
  if ((flags & 1u) != 0u && !Align16(offset, &offset)) return false;
  if (offset > drm.size()) return false;

  for (const DrmSectionHeader& header : headers) {
    // Resolver headers are not needed to decode the texture payload, but their
    // exact packed length still precedes every data section.
    const uint32_t resolver_size = header.flags >> 8u;
    if (resolver_size > drm.size() - offset) return false;
    offset += resolver_size;
    if ((flags & 1u) != 0u && !Align16(offset, &offset)) return false;
    if (offset > drm.size() || header.data_size > drm.size() - offset) {
      return false;
    }

    if (header.type == kRenderResourceSectionType &&
        (header.unknown05 & 1u) == 0u) {
      TexturePayload payload;
      if (ParsePcd9(drm.data() + offset, header.data_size, &payload)) {
        textures->emplace(header.id, std::move(payload));
      }
    }
    offset += header.data_size;
    if ((flags & 1u) != 0u && !Align16(offset, &offset)) return false;
    if (offset > drm.size()) return false;
  }
  return true;
}

enum class ArchiveIndexResult {
  kSuccess,
  kArchiveEntryUnavailable,
  kCdrmInvalid,
  kDrmInvalid,
};

const char* ArchiveIndexResultName(ArchiveIndexResult result) {
  switch (result) {
    case ArchiveIndexResult::kSuccess: return "ok";
    case ArchiveIndexResult::kArchiveEntryUnavailable:
      return "BIGFILE entry unavailable";
    case ArchiveIndexResult::kCdrmInvalid: return "CDRM decode failed";
    case ArchiveIndexResult::kDrmInvalid: return "DRM/PCD9 parse failed";
  }
  return "unknown failure";
}

ArchiveIndexResult AppendArchivedTextures(
    const std::wstring& game_directory, const char* virtual_path,
    std::unordered_map<uint32_t, TexturePayload>* destination) {
  if (!virtual_path || !destination) return ArchiveIndexResult::kDrmInvalid;
  std::vector<uint8_t> compressed;
  std::vector<uint8_t> drm;
  std::unordered_map<uint32_t, TexturePayload> parsed;
  if (!ReadArchivedDrm(game_directory, virtual_path, &compressed))
    return ArchiveIndexResult::kArchiveEntryUnavailable;
  if (!DecompressCdrm(compressed, &drm))
    return ArchiveIndexResult::kCdrmInvalid;
  if (!ParseDrmTextures(drm, &parsed) || parsed.empty())
    return ArchiveIndexResult::kDrmInvalid;
  for (auto& pair : parsed) {
    destination->emplace(pair.first, std::move(pair.second));
  }
  return ArchiveIndexResult::kSuccess;
}

struct GpuTexture {
  ID3D11ShaderResourceView* view = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
};

}  // namespace

struct GameTextureArchive::Impl {
  enum class State { kUninitialized, kReady, kFailed };

  mutable std::mutex mutex;
  State state = State::kUninitialized;
  std::wstring game_directory;
  std::unordered_map<uint32_t, TexturePayload> textures;
  ID3D11Device* cached_device = nullptr;
  std::unordered_map<uint32_t, GpuTexture> gpu_cache;
  std::unordered_set<uint32_t> logged_missing_ids;
  bool logged_archive_failure = false;
  bool logged_device_failure = false;

  void ClearGpuCache() {
    for (auto& pair : gpu_cache) ReleaseAndNull(pair.second.view);
    gpu_cache.clear();
    ReleaseAndNull(cached_device);
    logged_device_failure = false;
  }

  void Reset() {
    ClearGpuCache();
    textures.clear();
    logged_missing_ids.clear();
    game_directory.clear();
    state = State::kUninitialized;
    logged_archive_failure = false;
  }

  bool InitializeLocked(const wchar_t* requested_directory) {
    if (state == State::kReady) return true;
    if (state == State::kFailed) return false;

    game_directory = requested_directory && *requested_directory ?
                         std::wstring(requested_directory) :
                         ExecutableDirectory();
    const ArchiveIndexResult pickup_result = game_directory.empty() ?
        ArchiveIndexResult::kArchiveEntryUnavailable :
        AppendArchivedTextures(game_directory, kPickupDatabasePath, &textures);
    if (pickup_result != ArchiveIndexResult::kSuccess) {
      textures.clear();
      state = State::kFailed;
      if (!logged_archive_failure) {
        DXS_WARN("game texture archive: failed to index pc-w\\pickup_database.drm (%s)",
                 ArchiveIndexResultName(pickup_result));
        logged_archive_failure = true;
      }
      return false;
    }

    // Active-augmentation, lock-state, and other vanilla HUD art lives in the
    // global Scaleform database rather than the pickup database.  Treat it as
    // an optional extension: item artwork must continue to work if a regional
    // build does not contain this archive under the expected name.
    const size_t pickup_count = textures.size();
    const ArchiveIndexResult scaleform_result =
        AppendArchivedTextures(game_directory, kGlobalScaleformDatabasePath,
                               &textures);
    const bool scaleform_indexed =
        scaleform_result == ArchiveIndexResult::kSuccess;

    // The 21 augmentation family icons and their upgrade icons are packaged
    // with GlobalPlayerInfo.  Indexing this small database lets the custom HUD
    // use the exact vanilla Typhoon/Cloak/Smart Vision/Run Silently artwork.
    const size_t after_scaleform_count = textures.size();
    const ArchiveIndexResult player_info_result =
        AppendArchivedTextures(game_directory, kGlobalPlayerInfoPath,
                               &textures);
    const bool player_info_indexed =
        player_info_result == ArchiveIndexResult::kSuccess;

    state = State::kReady;
    DXS_INFO("game texture archive: indexed %u pickup + %u Scaleform + %u augmentation textures",
             static_cast<unsigned>(pickup_count),
             static_cast<unsigned>(scaleform_indexed ?
                                       after_scaleform_count - pickup_count : 0u),
             static_cast<unsigned>(player_info_indexed ?
                                       textures.size() - after_scaleform_count : 0u));
    return true;
  }

  bool EnsureDevice(ID3D11Device* device) {
    if (!device) return false;
    if (cached_device == device) return true;
    ClearGpuCache();
    cached_device = device;
    cached_device->AddRef();
    return true;
  }

  bool CreateTexture(ID3D11Device* device, uint32_t id,
                     const TexturePayload& payload, GpuTexture* output) {
    if (!device || !output || payload.mips.empty() || payload.bytes.empty()) {
      return false;
    }

    std::vector<D3D11_SUBRESOURCE_DATA> initial_data(payload.mips.size());
    for (size_t index = 0; index < payload.mips.size(); ++index) {
      const MipLevel& mip = payload.mips[index];
      if (mip.offset > payload.bytes.size() ||
          mip.size > payload.bytes.size() - mip.offset) {
        return false;
      }
      initial_data[index].pSysMem = payload.bytes.data() + mip.offset;
      initial_data[index].SysMemPitch = mip.row_pitch;
      initial_data[index].SysMemSlicePitch = mip.size;
    }

    D3D11_TEXTURE2D_DESC descriptor = {};
    descriptor.Width = payload.width;
    descriptor.Height = payload.height;
    descriptor.MipLevels = static_cast<UINT>(payload.mips.size());
    descriptor.ArraySize = 1u;
    descriptor.Format = payload.format;
    descriptor.SampleDesc.Count = 1u;
    descriptor.Usage = D3D11_USAGE_IMMUTABLE;
    descriptor.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* texture = nullptr;
    HRESULT result = device->CreateTexture2D(&descriptor, initial_data.data(),
                                             &texture);
    if (FAILED(result) || !texture) return false;

    ID3D11ShaderResourceView* view = nullptr;
    result = device->CreateShaderResourceView(texture, nullptr, &view);
    texture->Release();
    if (FAILED(result) || !view) return false;

    output->view = view;
    output->width = payload.width;
    output->height = payload.height;
    return true;
  }
};

GameTextureArchive::GameTextureArchive() : impl_(new (std::nothrow) Impl()) {}

GameTextureArchive::~GameTextureArchive() {
  // Do not Release D3D COM objects from static destruction: DXHR may already
  // have unloaded d3d11.dll by then. Panel teardown calls Shutdown explicitly
  // while the device is alive. If abnormal process teardown skips that call,
  // allowing the OS to reclaim these few process-owned references is safer.
  delete impl_;
  impl_ = nullptr;
}

bool GameTextureArchive::Initialize(const wchar_t* game_directory) {
  if (!impl_) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->InitializeLocked(game_directory);
}

bool GameTextureArchive::AcquireTexture(ID3D11Device* device,
                                        uint32_t texture_id,
                                        GameArchiveTexture* output) {
  if (output) *output = {};
  if (!impl_ || !device || !output || texture_id == 0u ||
      texture_id == 0xffffffffu) {
    return false;
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->InitializeLocked(nullptr) || !impl_->EnsureDevice(device)) {
    return false;
  }

  const auto cached = impl_->gpu_cache.find(texture_id);
  if (cached != impl_->gpu_cache.end() && cached->second.view) {
    cached->second.view->AddRef();
    output->view = cached->second.view;
    output->width = cached->second.width;
    output->height = cached->second.height;
    output->texture_id = texture_id;
    return true;
  }

  const auto found = impl_->textures.find(texture_id);
  if (found == impl_->textures.end()) {
    if (impl_->logged_missing_ids.insert(texture_id).second) {
      DXS_WARN("game texture archive: RenderResource %08x is not in indexed game databases",
               static_cast<unsigned>(texture_id));
    }
    return false;
  }

  GpuTexture created;
  if (!impl_->CreateTexture(device, texture_id, found->second, &created)) {
    if (!impl_->logged_device_failure) {
      DXS_WARN("game texture archive: D3D11 upload failed (first ID %08x)",
               static_cast<unsigned>(texture_id));
      impl_->logged_device_failure = true;
    }
    return false;
  }

  const auto inserted = impl_->gpu_cache.emplace(texture_id, created);
  if (!inserted.second) {
    ReleaseAndNull(created.view);
  }
  GpuTexture& result = inserted.first->second;
  if (!result.view) return false;
  result.view->AddRef();
  output->view = result.view;
  output->width = result.width;
  output->height = result.height;
  output->texture_id = texture_id;
  return true;
}

void GameTextureArchive::Shutdown() {
  if (!impl_) return;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->Reset();
}

bool GameTextureArchive::ready() const {
  if (!impl_) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->state == Impl::State::kReady;
}

uint32_t GameTextureArchive::indexed_texture_count() const {
  if (!impl_) return 0u;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return static_cast<uint32_t>(impl_->textures.size());
}

GameTextureArchive& TheGameTextureArchive() {
  static GameTextureArchive archive;
  return archive;
}

}  // namespace dxs

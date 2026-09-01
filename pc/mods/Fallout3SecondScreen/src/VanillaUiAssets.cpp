#include "VanillaUiAssets.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Perf.h"

namespace fo3ds {
namespace {

constexpr uint32_t kBsaVersion = 104;
constexpr uint32_t kBsaDirectoryNames = 0x00000001u;
constexpr uint32_t kBsaFileNames = 0x00000002u;
constexpr uint32_t kBsaCompressed = 0x00000004u;
constexpr uint32_t kBsaEmbeddedNames = 0x00000100u;
constexpr uint32_t kFileCompressionToggle = 0x40000000u;
constexpr uint32_t kFileSizeMask = 0x3fffffffu;
constexpr size_t kBsaHeaderSize = 36;
constexpr size_t kBsaFolderRecordSize = 16;
constexpr size_t kBsaFileRecordSize = 16;
constexpr size_t kDdsHeaderSize = 128;
constexpr size_t kGuardSize = 16;
constexpr uint8_t kLeadingGuard = 0xa5;
constexpr uint8_t kTrailingGuard = 0x5a;
constexpr uint64_t kMaxMetadataBytes = 64ull * 1024ull * 1024ull;
constexpr uint64_t kMaxStoredBytes = 128ull * 1024ull * 1024ull;
constexpr uint64_t kMaxDecodedBytes = 128ull * 1024ull * 1024ull;
constexpr uint32_t kMaxFolders = 100000;
constexpr uint32_t kMaxFiles = 1000000;
constexpr uint32_t kMaxDimension = 8192;
constexpr uint64_t kMaxPixels = 16ull * 1024ull * 1024ull;
constexpr size_t kMaxCachedAssets = 256;
constexpr uint64_t kMaxCachedAssetBytes = 96ull * 1024ull * 1024ull;
constexpr size_t kMaxTintsPerAsset = 8;

bool CheckedAdd(uint64_t left, uint64_t right, uint64_t* result) {
  if (!result || right > std::numeric_limits<uint64_t>::max() - left)
    return false;
  *result = left + right;
  return true;
}

bool CheckedMultiply(uint64_t left, uint64_t right, uint64_t* result) {
  if (!result || (left != 0 && right > std::numeric_limits<uint64_t>::max() / left))
    return false;
  *result = left * right;
  return true;
}

uint16_t ReadU16(const uint8_t* source) {
  return static_cast<uint16_t>(source[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(source[1]) << 8);
}

uint32_t ReadU32(const uint8_t* source) {
  return static_cast<uint32_t>(source[0]) |
         (static_cast<uint32_t>(source[1]) << 8) |
         (static_cast<uint32_t>(source[2]) << 16) |
         (static_cast<uint32_t>(source[3]) << 24);
}

uint64_t ReadU64(const uint8_t* source) {
  uint64_t value = 0;
  for (unsigned int index = 0; index < 8; ++index)
    value |= static_cast<uint64_t>(source[index]) << (index * 8);
  return value;
}

class GuardedBytes {
 public:
  GuardedBytes() = default;
  explicit GuardedBytes(size_t size) { Reset(size); }

  bool Reset(size_t size) {
    active_ = false;
    payload_size_ = 0;
    if (size > std::numeric_limits<size_t>::max() - kGuardSize * 2)
      return false;
    try {
      storage_.assign(size + kGuardSize * 2, 0);
    } catch (...) {
      storage_.clear();
      return false;
    }
    std::fill(storage_.begin(), storage_.begin() + kGuardSize, kLeadingGuard);
    std::fill(storage_.end() - kGuardSize, storage_.end(), kTrailingGuard);
    payload_size_ = size;
    active_ = true;
    return true;
  }

  uint8_t* data() {
    return active_ ? storage_.data() + kGuardSize : nullptr;
  }
  const uint8_t* data() const {
    return active_ ? storage_.data() + kGuardSize : nullptr;
  }
  size_t size() const { return payload_size_; }

  bool GuardsIntact() const {
    if (!active_ || storage_.size() != payload_size_ + kGuardSize * 2)
      return false;
    return std::all_of(storage_.begin(), storage_.begin() + kGuardSize,
                       [](uint8_t byte) { return byte == kLeadingGuard; }) &&
           std::all_of(storage_.end() - kGuardSize, storage_.end(),
                       [](uint8_t byte) { return byte == kTrailingGuard; });
  }

 private:
  std::vector<uint8_t> storage_;
  size_t payload_size_ = 0;
  bool active_ = false;
};

class ScopedHandle {
 public:
  ScopedHandle() = default;
  explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
  ~ScopedHandle() { Reset(); }
  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;

  ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = INVALID_HANDLE_VALUE;
  }
  ScopedHandle& operator=(ScopedHandle&& other) noexcept {
    if (this != &other) {
      Reset();
      handle_ = other.handle_;
      other.handle_ = INVALID_HANDLE_VALUE;
    }
    return *this;
  }

  HANDLE get() const { return handle_; }
  bool valid() const {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }
  void Reset(HANDLE handle = INVALID_HANDLE_VALUE) {
    if (valid()) CloseHandle(handle_);
    handle_ = handle;
  }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

bool GetHandleSize(HANDLE handle, uint64_t* result) {
  if (!result) return false;
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0) return false;
  *result = static_cast<uint64_t>(size.QuadPart);
  return true;
}

bool ReadHandleRange(HANDLE handle, uint64_t file_size, uint64_t offset,
                     size_t byte_count, GuardedBytes* result) {
  if (!result || handle == nullptr || handle == INVALID_HANDLE_VALUE ||
      byte_count > static_cast<size_t>(std::numeric_limits<DWORD>::max()))
    return false;
  uint64_t end = 0;
  if (!CheckedAdd(offset, static_cast<uint64_t>(byte_count), &end) ||
      end > file_size || !result->Reset(byte_count))
    return false;
  LARGE_INTEGER position = {};
  position.QuadPart = static_cast<LONGLONG>(offset);
  if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) return false;
  DWORD read = 0;
  if (byte_count != 0 &&
      (!ReadFile(handle, result->data(), static_cast<DWORD>(byte_count), &read,
                 nullptr) ||
       read != byte_count))
    return false;
  return result->GuardsIntact();
}

bool ReadWholeFile(const std::wstring& path, GuardedBytes* result) {
  if (!result) return false;
  ScopedHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                nullptr));
  uint64_t size = 0;
  if (!file.valid() || !GetHandleSize(file.get(), &size) ||
      size == 0 || size > kMaxStoredBytes ||
      size > std::numeric_limits<size_t>::max())
    return false;
  return ReadHandleRange(file.get(), size, 0, static_cast<size_t>(size), result);
}

std::wstring GetExecutableDirectory() {
  std::vector<wchar_t> path(512, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length == 0) return {};
    if (length < path.size() - 1) {
      std::wstring result(path.data(), length);
      const size_t slash = result.find_last_of(L"\\/");
      return slash == std::wstring::npos ? std::wstring() : result.substr(0, slash);
    }
    if (path.size() >= 32768) return {};
    path.resize(std::min<size_t>(path.size() * 2, 32768), L'\0');
  }
}

std::wstring TrimTrailingSeparators(std::wstring path) {
  while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/'))
    path.pop_back();
  return path;
}

bool NormalizeRelativePath(const wchar_t* source, std::wstring* result) {
  if (!source || !result || !*source) return false;
  size_t source_length = 0;
  while (source_length <= 1024 && source[source_length]) ++source_length;
  if (!source_length || source_length > 1024) return false;
  try {
  std::wstring normalized;
  normalized.reserve(source_length);
  bool at_segment_start = true;
  std::wstring segment;
  const auto append_segment = [&]() -> bool {
    if (segment.empty() || segment == L"." || segment == L"..") return false;
    if (!normalized.empty()) normalized.push_back(L'\\');
    normalized += segment;
    segment.clear();
    return true;
  };
  for (const wchar_t* cursor = source; *cursor; ++cursor) {
    wchar_t character = *cursor;
    if (character == L'/' || character == L'\\') {
      if (at_segment_start || !append_segment()) return false;
      at_segment_start = true;
      continue;
    }
    if (character < 32 || character == L':' || character == L'*' ||
        character == L'?' || character == L'"' || character == L'<' ||
        character == L'>' || character == L'|')
      return false;
    if (segment.size() >= 255 || normalized.size() + segment.size() >= 1024)
      return false;
    if (character >= L'A' && character <= L'Z')
      character = static_cast<wchar_t>(character - L'A' + L'a');
    segment.push_back(character);
    at_segment_start = false;
  }
  if (at_segment_start || !append_segment()) return false;
  if (normalized.size() > 5 && normalized.compare(0, 5, L"data\\") == 0)
    normalized.erase(0, 5);
  if (normalized.empty()) return false;
  *result = std::move(normalized);
  return true;
  } catch (...) {
    return false;
  }
}

std::wstring DecodeBsaName(const uint8_t* source, size_t length) {
  if (!source || length > 1024) return {};
  std::wstring result;
  result.reserve(length);
  for (size_t index = 0; index < length; ++index) {
    const uint8_t character = source[index];
    if (character == 0) break;
    result.push_back(static_cast<wchar_t>(character));
  }
  return result;
}

struct BsaRecord {
  uint32_t size_flags = 0;
  uint32_t offset = 0;
};

struct RawFolder {
  uint32_t file_count = 0;
  uint32_t stored_offset = 0;
};

struct RawFile {
  uint32_t size_flags = 0;
  uint32_t offset = 0;
};

class BsaArchive {
 public:
  bool Open(const std::wstring& path) try {
    Close();
    file_.Reset(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                            nullptr));
    if (!file_.valid() || !GetHandleSize(file_.get(), &file_size_) ||
        file_size_ < kBsaHeaderSize)
      return false;

    GuardedBytes header;
    if (!ReadHandleRange(file_.get(), file_size_, 0, kBsaHeaderSize, &header) ||
        std::memcmp(header.data(), "BSA\0", 4) != 0 ||
        ReadU32(header.data() + 4) != kBsaVersion) {
      Close();
      return false;
    }
    const uint32_t folder_record_offset = ReadU32(header.data() + 8);
    flags_ = ReadU32(header.data() + 12);
    const uint32_t folder_count = ReadU32(header.data() + 16);
    const uint32_t file_count = ReadU32(header.data() + 20);
    const uint32_t folder_name_bytes = ReadU32(header.data() + 24);
    const uint32_t file_name_bytes = ReadU32(header.data() + 28);
    if (folder_record_offset < kBsaHeaderSize ||
        (flags_ & (kBsaDirectoryNames | kBsaFileNames)) !=
            (kBsaDirectoryNames | kBsaFileNames) ||
        folder_count > kMaxFolders ||
        file_count > kMaxFiles || (file_count != 0 && folder_count == 0)) {
      Close();
      return false;
    }

    uint64_t metadata_size = folder_record_offset;
    uint64_t amount = 0;
    if (!CheckedMultiply(folder_count, kBsaFolderRecordSize, &amount) ||
        !CheckedAdd(metadata_size, amount, &metadata_size) ||
        !CheckedAdd(metadata_size, folder_name_bytes, &metadata_size) ||
        !CheckedAdd(metadata_size, folder_count, &metadata_size) ||
        !CheckedMultiply(file_count, kBsaFileRecordSize, &amount) ||
        !CheckedAdd(metadata_size, amount, &metadata_size) ||
        !CheckedAdd(metadata_size, file_name_bytes, &metadata_size) ||
        metadata_size > kMaxMetadataBytes || metadata_size > file_size_ ||
        metadata_size > std::numeric_limits<size_t>::max()) {
      Close();
      return false;
    }

    GuardedBytes metadata;
    if (!ReadHandleRange(file_.get(), file_size_, 0,
                         static_cast<size_t>(metadata_size), &metadata)) {
      Close();
      return false;
    }
    const uint8_t* bytes = metadata.data();
    std::vector<RawFolder> raw_folders;
    try {
      raw_folders.reserve(folder_count);
    } catch (...) {
      Close();
      return false;
    }
    uint64_t record_cursor = folder_record_offset;
    uint64_t total_records = 0;
    for (uint32_t index = 0; index < folder_count; ++index) {
      const uint8_t* record = bytes + static_cast<size_t>(record_cursor);
      const RawFolder folder = {ReadU32(record + 8), ReadU32(record + 12)};
      if (!CheckedAdd(total_records, folder.file_count, &total_records) ||
          total_records > file_count) {
        Close();
        return false;
      }
      raw_folders.push_back(folder);
      record_cursor += kBsaFolderRecordSize;
    }
    if (total_records != file_count) {
      Close();
      return false;
    }

    std::vector<std::pair<std::wstring, std::vector<RawFile>>> folders;
    std::vector<std::pair<uint64_t, uint64_t>> folder_intervals;
    try {
      folders.reserve(folder_count);
      folder_intervals.reserve(folder_count);
    } catch (...) {
      Close();
      return false;
    }
    uint64_t filename_table = 0;
    for (const RawFolder& raw_folder : raw_folders) {
      if (raw_folder.stored_offset < file_name_bytes) {
        Close();
        return false;
      }
      const uint64_t folder_block =
          static_cast<uint64_t>(raw_folder.stored_offset) - file_name_bytes;
      if (folder_block >= metadata_size) {
        Close();
        return false;
      }
      const uint8_t name_length = bytes[static_cast<size_t>(folder_block)];
      uint64_t file_records_begin = 0;
      if (name_length == 0 ||
          !CheckedAdd(folder_block, 1u + name_length, &file_records_begin)) {
        Close();
        return false;
      }
      uint64_t file_record_bytes = 0;
      uint64_t file_records_end = 0;
      if (!CheckedMultiply(raw_folder.file_count, kBsaFileRecordSize,
                           &file_record_bytes) ||
          !CheckedAdd(file_records_begin, file_record_bytes, &file_records_end) ||
          file_records_end > metadata_size) {
        Close();
        return false;
      }
      const uint64_t folder_records_end =
          static_cast<uint64_t>(folder_record_offset) +
          static_cast<uint64_t>(folder_count) * kBsaFolderRecordSize;
      if (folder_block < folder_records_end) {
        Close();
        return false;
      }
      folder_intervals.emplace_back(folder_block, file_records_end);
      std::wstring folder_name = DecodeBsaName(
          bytes + static_cast<size_t>(folder_block + 1), name_length);
      std::wstring normalized_folder;
      if (!NormalizeRelativePath(folder_name.c_str(), &normalized_folder)) {
        Close();
        return false;
      }
      std::vector<RawFile> files;
      try {
        files.reserve(raw_folder.file_count);
      } catch (...) {
        Close();
        return false;
      }
      for (uint32_t index = 0; index < raw_folder.file_count; ++index) {
        const uint8_t* record =
            bytes + static_cast<size_t>(file_records_begin) +
            static_cast<size_t>(index) * kBsaFileRecordSize;
        const RawFile file = {ReadU32(record + 8), ReadU32(record + 12)};
        const uint32_t stored_size = file.size_flags & kFileSizeMask;
        uint64_t payload_end = 0;
        if (stored_size == 0 || stored_size > kMaxStoredBytes ||
            file.offset < metadata_size ||
            !CheckedAdd(file.offset, stored_size, &payload_end) ||
            payload_end > file_size_) {
          Close();
          return false;
        }
        files.push_back(file);
      }
      filename_table = std::max(filename_table, file_records_end);
      folders.emplace_back(std::move(normalized_folder), std::move(files));
    }

    std::sort(folder_intervals.begin(), folder_intervals.end());
    for (size_t index = 1; index < folder_intervals.size(); ++index) {
      if (folder_intervals[index].first < folder_intervals[index - 1].second) {
        Close();
        return false;
      }
    }

    uint64_t filename_table_end = 0;
    if (!CheckedAdd(filename_table, file_name_bytes, &filename_table_end) ||
        filename_table_end > metadata_size) {
      Close();
      return false;
    }
    uint64_t name_cursor = filename_table;
    try {
      records_.reserve(file_count);
    } catch (...) {
      Close();
      return false;
    }
    uint32_t parsed_files = 0;
    for (const auto& folder : folders) {
      for (const RawFile& file : folder.second) {
        if (name_cursor >= filename_table_end) {
          Close();
          return false;
        }
        uint64_t end = name_cursor;
        while (end < filename_table_end && bytes[static_cast<size_t>(end)] != 0)
          ++end;
        if (end == name_cursor || end >= filename_table_end) {
          Close();
          return false;
        }
        const std::wstring filename = DecodeBsaName(
            bytes + static_cast<size_t>(name_cursor),
            static_cast<size_t>(end - name_cursor));
        std::wstring combined = folder.first;
        combined.push_back(L'\\');
        combined += filename;
        std::wstring normalized;
        if (!NormalizeRelativePath(combined.c_str(), &normalized)) {
          Close();
          return false;
        }
        const auto inserted = records_.emplace(
            std::move(normalized), BsaRecord{file.size_flags, file.offset});
        if (!inserted.second) {
          Close();
          return false;
        }
        name_cursor = end + 1;
        ++parsed_files;
      }
    }
    if (parsed_files != file_count || records_.size() != file_count ||
        name_cursor != filename_table_end || !metadata.GuardsIntact()) {
      Close();
      return false;
    }
    return true;
  } catch (...) {
    Close();
    return false;
  }

  bool Read(const std::wstring& normalized_path, GuardedBytes* result) {
    if (!result || !file_.valid()) return false;
    const auto found = records_.find(normalized_path);
    if (found == records_.end()) return false;
    const uint32_t stored_size = found->second.size_flags & kFileSizeMask;
    GuardedBytes stored;
    if (!ReadHandleRange(file_.get(), file_size_, found->second.offset,
                         stored_size, &stored))
      return false;
    size_t cursor = 0;
    if ((flags_ & kBsaEmbeddedNames) != 0) {
      if (stored.size() < 1) return false;
      const size_t embedded_bytes = static_cast<size_t>(stored.data()[0]) + 1;
      if (embedded_bytes > stored.size()) return false;
      cursor = embedded_bytes;
    }
    const bool compressed = ((flags_ & kBsaCompressed) != 0) !=
                            ((found->second.size_flags &
                              kFileCompressionToggle) != 0);
    if (!compressed) {
      const size_t output_size = stored.size() - cursor;
      if (output_size == 0 || output_size > kMaxDecodedBytes ||
          !result->Reset(output_size))
        return false;
      std::memcpy(result->data(), stored.data() + cursor, output_size);
      return stored.GuardsIntact() && result->GuardsIntact();
    }
    if (stored.size() - cursor < 4) return false;
    const uint32_t expected_size = ReadU32(stored.data() + cursor);
    cursor += 4;
    const size_t compressed_size = stored.size() - cursor;
    if (expected_size == 0 || expected_size > kMaxDecodedBytes ||
        compressed_size == 0 || !result->Reset(expected_size))
      return false;
    uLongf actual_size = static_cast<uLongf>(expected_size);
    const int status = uncompress(
        result->data(), &actual_size, stored.data() + cursor,
        static_cast<uLong>(compressed_size));
    return status == Z_OK && actual_size == expected_size &&
           stored.GuardsIntact() && result->GuardsIntact();
  }

  bool valid() const { return file_.valid(); }

  void Close() {
    records_.clear();
    flags_ = 0;
    file_size_ = 0;
    file_.Reset();
  }

 private:
  ScopedHandle file_;
  uint64_t file_size_ = 0;
  uint32_t flags_ = 0;
  std::unordered_map<std::wstring, BsaRecord> records_;
};

struct Color {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha;
};

Color Decode565(uint16_t packed) {
  const uint8_t red5 = static_cast<uint8_t>((packed >> 11) & 31u);
  const uint8_t green6 = static_cast<uint8_t>((packed >> 5) & 63u);
  const uint8_t blue5 = static_cast<uint8_t>(packed & 31u);
  return {static_cast<uint8_t>((red5 << 3) | (red5 >> 2)),
          static_cast<uint8_t>((green6 << 2) | (green6 >> 4)),
          static_cast<uint8_t>((blue5 << 3) | (blue5 >> 2)), 255};
}

Color Mix(const Color& left, const Color& right, unsigned int left_weight,
          unsigned int right_weight, unsigned int divisor) {
  return {static_cast<uint8_t>((left.red * left_weight +
                                right.red * right_weight) /
                               divisor),
          static_cast<uint8_t>((left.green * left_weight +
                                right.green * right_weight) /
                               divisor),
          static_cast<uint8_t>((left.blue * left_weight +
                                right.blue * right_weight) /
                               divisor),
          255};
}

void DecodeColorBlock(const uint8_t* block, bool allow_dxt1_alpha,
                      Color* palette, uint32_t* indices) {
  const uint16_t packed0 = ReadU16(block);
  const uint16_t packed1 = ReadU16(block + 2);
  palette[0] = Decode565(packed0);
  palette[1] = Decode565(packed1);
  if (!allow_dxt1_alpha || packed0 > packed1) {
    palette[2] = Mix(palette[0], palette[1], 2, 1, 3);
    palette[3] = Mix(palette[0], palette[1], 1, 2, 3);
  } else {
    palette[2] = Mix(palette[0], palette[1], 1, 1, 2);
    palette[3] = {0, 0, 0, 0};
  }
  *indices = ReadU32(block + 4);
}

struct DecodedDds {
  int width = 0;
  int height = 0;
  GuardedBytes bgra;
};

bool DecodeDds(const GuardedBytes& dds, DecodedDds* result) {
  if (!result || dds.size() < kDdsHeaderSize || !dds.GuardsIntact() ||
      std::memcmp(dds.data(), "DDS ", 4) != 0 ||
      ReadU32(dds.data() + 4) != 124 || ReadU32(dds.data() + 76) != 32)
    return false;
  const uint32_t height = ReadU32(dds.data() + 12);
  const uint32_t width = ReadU32(dds.data() + 16);
  const uint32_t fourcc = ReadU32(dds.data() + 84);
  constexpr uint32_t kDxt1 = 0x31545844u;
  constexpr uint32_t kDxt3 = 0x33545844u;
  constexpr uint32_t kDxt5 = 0x35545844u;
  if (width == 0 || height == 0 || width > kMaxDimension ||
      height > kMaxDimension ||
      (fourcc != kDxt1 && fourcc != kDxt3 && fourcc != kDxt5))
    return false;
  uint64_t pixel_count = 0;
  uint64_t pixel_bytes = 0;
  if (!CheckedMultiply(width, height, &pixel_count) || pixel_count > kMaxPixels ||
      !CheckedMultiply(pixel_count, 4, &pixel_bytes) ||
      pixel_bytes > std::numeric_limits<size_t>::max())
    return false;
  const uint64_t blocks_wide = (static_cast<uint64_t>(width) + 3) / 4;
  const uint64_t blocks_high = (static_cast<uint64_t>(height) + 3) / 4;
  const uint64_t block_size = fourcc == kDxt1 ? 8 : 16;
  uint64_t block_count = 0;
  uint64_t encoded_size = 0;
  uint64_t required_size = 0;
  if (!CheckedMultiply(blocks_wide, blocks_high, &block_count) ||
      !CheckedMultiply(block_count, block_size, &encoded_size) ||
      !CheckedAdd(kDdsHeaderSize, encoded_size, &required_size) ||
      required_size > dds.size() ||
      !result->bgra.Reset(static_cast<size_t>(pixel_bytes)))
    return false;

  const uint8_t* block = dds.data() + kDdsHeaderSize;
  for (uint64_t block_y = 0; block_y < blocks_high; ++block_y) {
    for (uint64_t block_x = 0; block_x < blocks_wide;
         ++block_x, block += block_size) {
      Color colors[4] = {};
      uint32_t color_indices = 0;
      const uint8_t* color_block = fourcc == kDxt1 ? block : block + 8;
      DecodeColorBlock(color_block, fourcc == kDxt1, colors, &color_indices);
      uint8_t alpha_values[8] = {};
      uint64_t alpha_indices = 0;
      uint64_t explicit_alpha = 0;
      if (fourcc == kDxt3) {
        explicit_alpha = ReadU64(block);
      } else if (fourcc == kDxt5) {
        alpha_values[0] = block[0];
        alpha_values[1] = block[1];
        if (alpha_values[0] > alpha_values[1]) {
          for (unsigned int index = 1; index <= 6; ++index) {
            alpha_values[index + 1] = static_cast<uint8_t>(
                ((7 - index) * alpha_values[0] + index * alpha_values[1]) / 7);
          }
        } else {
          for (unsigned int index = 1; index <= 4; ++index) {
            alpha_values[index + 1] = static_cast<uint8_t>(
                ((5 - index) * alpha_values[0] + index * alpha_values[1]) / 5);
          }
          alpha_values[6] = 0;
          alpha_values[7] = 255;
        }
        for (unsigned int index = 0; index < 6; ++index)
          alpha_indices |= static_cast<uint64_t>(block[index + 2]) << (index * 8);
      }
      for (unsigned int pixel = 0; pixel < 16; ++pixel) {
        const uint32_t local_x = pixel & 3u;
        const uint32_t local_y = pixel >> 2;
        const uint64_t x = block_x * 4 + local_x;
        const uint64_t y = block_y * 4 + local_y;
        if (x >= width || y >= height) continue;
        Color color = colors[(color_indices >> (pixel * 2)) & 3u];
        if (fourcc == kDxt3) {
          const uint8_t alpha =
              static_cast<uint8_t>((explicit_alpha >> (pixel * 4)) & 15u);
          color.alpha = static_cast<uint8_t>(alpha * 17u);
        } else if (fourcc == kDxt5) {
          color.alpha = alpha_values[(alpha_indices >> (pixel * 3)) & 7u];
        }
        const size_t output =
            static_cast<size_t>((y * width + x) * static_cast<uint64_t>(4));
        result->bgra.data()[output] = color.blue;
        result->bgra.data()[output + 1] = color.green;
        result->bgra.data()[output + 2] = color.red;
        result->bgra.data()[output + 3] = color.alpha;
      }
    }
  }
  if (!result->bgra.GuardsIntact() || !dds.GuardsIntact()) return false;
  result->width = static_cast<int>(width);
  result->height = static_cast<int>(height);
  return true;
}

struct TintDib {
  HDC dc = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previous = nullptr;

  ~TintDib() {
    if (dc && previous) SelectObject(dc, previous);
    if (dc) DeleteDC(dc);
    if (bitmap) DeleteObject(bitmap);
  }
  TintDib(const TintDib&) = delete;
  TintDib& operator=(const TintDib&) = delete;
  TintDib() = default;
};

struct Asset {
  int width = 0;
  int height = 0;
  int content_x = 0;
  int content_y = 0;
  int content_width = 0;
  int content_height = 0;
  GuardedBytes bgra;
  std::unordered_map<uint32_t, std::unique_ptr<TintDib>> tints;
  uint64_t last_use = 0;
};

void MeasureVisibleContent(Asset* asset) {
  if (!asset || asset->width <= 0 || asset->height <= 0 ||
      !asset->bgra.GuardsIntact())
    return;
  int left = asset->width;
  int top = asset->height;
  int right = -1;
  int bottom = -1;
  for (int y = 0; y < asset->height; ++y) {
    for (int x = 0; x < asset->width; ++x) {
      const size_t alpha =
          (static_cast<size_t>(y) * asset->width + x) * 4u + 3u;
      if (asset->bgra.data()[alpha] == 0) continue;
      left = std::min(left, x);
      top = std::min(top, y);
      right = std::max(right, x);
      bottom = std::max(bottom, y);
    }
  }
  if (right >= left && bottom >= top) {
    asset->content_x = left;
    asset->content_y = top;
    asset->content_width = right - left + 1;
    asset->content_height = bottom - top + 1;
  } else {
    asset->content_width = asset->width;
    asset->content_height = asset->height;
  }
}

std::unique_ptr<TintDib> CreateTintDib(const Asset& asset, COLORREF tint) {
  if (asset.width <= 0 || asset.height <= 0 || !asset.bgra.GuardsIntact())
    return nullptr;
  std::unique_ptr<TintDib> dib(new (std::nothrow) TintDib());
  if (!dib) return nullptr;
  dib->dc = CreateCompatibleDC(nullptr);
  if (!dib->dc) return nullptr;
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = asset.width;
  info.bmiHeader.biHeight = -asset.height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* raw_bits = nullptr;
  dib->bitmap = CreateDIBSection(dib->dc, &info, DIB_RGB_COLORS, &raw_bits,
                                 nullptr, 0);
  if (!dib->bitmap || !raw_bits) return nullptr;
  dib->previous = SelectObject(dib->dc, dib->bitmap);
  if (!dib->previous || dib->previous == HGDI_ERROR) {
    dib->previous = nullptr;
    return nullptr;
  }

  const uint32_t tint_red = GetRValue(tint);
  const uint32_t tint_green = GetGValue(tint);
  const uint32_t tint_blue = GetBValue(tint);
  uint8_t* destination = static_cast<uint8_t*>(raw_bits);
  const size_t pixels = static_cast<size_t>(asset.width) * asset.height;
  for (size_t index = 0; index < pixels; ++index) {
    const uint8_t* source = asset.bgra.data() + index * 4;
    const uint32_t alpha = source[3];
    const uint32_t blue = (static_cast<uint32_t>(source[0]) * tint_blue + 127) / 255;
    const uint32_t green =
        (static_cast<uint32_t>(source[1]) * tint_green + 127) / 255;
    const uint32_t red = (static_cast<uint32_t>(source[2]) * tint_red + 127) / 255;
    destination[index * 4] = static_cast<uint8_t>((blue * alpha + 127) / 255);
    destination[index * 4 + 1] =
        static_cast<uint8_t>((green * alpha + 127) / 255);
    destination[index * 4 + 2] =
        static_cast<uint8_t>((red * alpha + 127) / 255);
    destination[index * 4 + 3] = static_cast<uint8_t>(alpha);
  }
  return dib;
}

struct CacheState {
  struct ArchiveSlot {
    std::wstring path;
    std::unique_ptr<BsaArchive> archive;
    bool attempted = false;
  };
  std::mutex mutex;
  bool initialized = false;
  std::wstring game_directory;
  std::wstring data_directory;
  std::vector<ArchiveSlot> archives;
  std::unordered_map<std::wstring, std::shared_ptr<Asset>> assets;
  uint64_t cached_asset_bytes = 0;
  uint64_t access_clock = 0;
  uint64_t file_reads = 0;
  uint64_t dds_decodes = 0;
};

void EraseCachedAsset(
    CacheState& state,
    std::unordered_map<std::wstring, std::shared_ptr<Asset>>::iterator entry) {
  if (entry->second) {
    const uint64_t bytes = entry->second->bgra.size();
    state.cached_asset_bytes = bytes <= state.cached_asset_bytes
        ? state.cached_asset_bytes - bytes : 0;
  }
  state.assets.erase(entry);
}

bool EvictLeastRecentlyUsedAsset(CacheState& state) {
  if (state.assets.empty()) return false;
  auto victim = state.assets.end();
  for (auto entry = state.assets.begin(); entry != state.assets.end(); ++entry) {
    // Failed resolutions do not own useful data and are the first eviction
    // candidates once a large inventory has visited the cache.
    if (!entry->second) {
      victim = entry;
      break;
    }
    if (victim == state.assets.end() ||
        entry->second->last_use < victim->second->last_use)
      victim = entry;
  }
  if (victim == state.assets.end()) return false;
  EraseCachedAsset(state, victim);
  return true;
}

CacheState& State() {
  // Lifetime is explicit: ShutdownVanillaUiAssets releases all handles/GDI
  // objects. Leaking only this empty process-lifetime shell avoids destructor
  // ordering against Win32 during DLL process detach.
  static CacheState* state = new (std::nothrow) CacheState();
  static CacheState fallback;
  return state ? *state : fallback;
}

bool InitializeLocked(CacheState& state, const wchar_t* game_directory) {
  if (state.initialized && (!game_directory || !*game_directory)) return true;
  std::wstring directory =
      game_directory && *game_directory ? game_directory : GetExecutableDirectory();
  directory = TrimTrailingSeparators(std::move(directory));
  if (directory.empty()) return false;
  if (state.initialized && state.game_directory == directory) return true;
  state.assets.clear();
  state.cached_asset_bytes = 0;
  state.access_clock = 0;
  state.archives.clear();
  state.game_directory = directory;
  state.data_directory = directory + L"\\Data";
  constexpr const wchar_t* kTextureArchives[] = {
      L"Fallout - Textures.bsa", L"Anchorage - Main.bsa",
      L"ThePitt - Main.bsa", L"BrokenSteel - Main.bsa",
      L"PointLookout - Main.bsa", L"Zeta - Main.bsa"};
  try {
    state.archives.reserve(sizeof(kTextureArchives) /
                           sizeof(kTextureArchives[0]));
    for (const wchar_t* name : kTextureArchives) {
      CacheState::ArchiveSlot slot;
      slot.path = state.data_directory + L"\\" + name;
      state.archives.emplace_back(std::move(slot));
    }
  } catch (...) {
    state.archives.clear();
  }
  state.initialized = true;
  return true;
}

std::shared_ptr<Asset> LoadAssetLocked(CacheState& state,
                                       const std::wstring& normalized_path) {
  const auto cached = state.assets.find(normalized_path);
  if (cached != state.assets.end()) {
    if (cached->second) cached->second->last_use = ++state.access_clock;
    return cached->second;
  }
  ScopedPerformance asset_load(PerfDomain::kUiAssetLoad);
  while (state.assets.size() >= kMaxCachedAssets &&
         EvictLeastRecentlyUsedAsset(state)) {
  }

  GuardedBytes dds;
  const std::wstring loose_path = state.data_directory + L"\\" + normalized_path;
  bool loaded = ReadWholeFile(loose_path, &dds);
  if (loaded) {
    ++state.file_reads;
  } else {
    for (CacheState::ArchiveSlot& slot : state.archives) {
      if (!slot.attempted) {
        slot.attempted = true;
        slot.archive = std::unique_ptr<BsaArchive>(
            new (std::nothrow) BsaArchive());
        if (!slot.archive || !slot.archive->Open(slot.path))
          slot.archive.reset();
      }
      if (slot.archive && slot.archive->valid() &&
          slot.archive->Read(normalized_path, &dds)) {
        loaded = true;
        ++state.file_reads;
        break;
      }
    }
  }
  std::shared_ptr<Asset> asset;
  if (loaded) {
    DecodedDds decoded;
    ++state.dds_decodes;
    if (DecodeDds(dds, &decoded)) {
      try {
        asset = std::make_shared<Asset>();
        asset->width = decoded.width;
        asset->height = decoded.height;
        asset->bgra = std::move(decoded.bgra);
        MeasureVisibleContent(asset.get());
        asset->last_use = ++state.access_clock;
      } catch (...) {
        asset.reset();
      }
    }
  }
  if (asset) {
    const uint64_t bytes = asset->bgra.size();
    if (bytes > kMaxCachedAssetBytes) return asset;
    while (state.cached_asset_bytes > kMaxCachedAssetBytes - bytes &&
           EvictLeastRecentlyUsedAsset(state)) {
    }
    state.cached_asset_bytes += bytes;
  }
  try {
    state.assets.emplace(normalized_path, asset);
  } catch (...) {
    if (asset) {
      const uint64_t bytes = asset->bgra.size();
      state.cached_asset_bytes = bytes <= state.cached_asset_bytes
          ? state.cached_asset_bytes - bytes : 0;
    }
    return nullptr;
  }
  return asset;
}

}  // namespace

bool InitializeVanillaUiAssets(const wchar_t* game_directory) {
  try {
  CacheState& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  return InitializeLocked(state, game_directory);
  } catch (...) {
    return false;
  }
}

bool PreloadVanillaUiAsset(const wchar_t* game_relative_path) {
  try {
  std::wstring normalized;
  if (!NormalizeRelativePath(game_relative_path, &normalized)) return false;
  CacheState& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!InitializeLocked(state, nullptr)) return false;
  return static_cast<bool>(LoadAssetLocked(state, normalized));
  } catch (...) {
    return false;
  }
}

bool DrawVanillaUiAsset(HDC target, const wchar_t* game_relative_path,
                        const RECT& destination, COLORREF tint, BYTE opacity) {
  try {
  const int64_t destination_width =
      static_cast<int64_t>(destination.right) - destination.left;
  const int64_t destination_height =
      static_cast<int64_t>(destination.bottom) - destination.top;
  if (!target || destination_width <= 0 || destination_height <= 0 ||
      destination_width > std::numeric_limits<int>::max() ||
      destination_height > std::numeric_limits<int>::max() || opacity == 0)
    return false;
  std::wstring normalized;
  if (!NormalizeRelativePath(game_relative_path, &normalized)) return false;
  CacheState& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!InitializeLocked(state, nullptr)) return false;
  const std::shared_ptr<Asset> asset = LoadAssetLocked(state, normalized);
  if (!asset || !asset->bgra.GuardsIntact()) return false;
  const uint32_t tint_key = static_cast<uint32_t>(tint) & 0x00ffffffu;
  auto found = asset->tints.find(tint_key);
  if (found == asset->tints.end()) {
    if (asset->tints.size() >= kMaxTintsPerAsset)
      asset->tints.erase(asset->tints.begin());
    std::unique_ptr<TintDib> dib = CreateTintDib(*asset, tint);
    if (!dib) return false;
    found = asset->tints.emplace(tint_key, std::move(dib)).first;
  }
  const BLENDFUNCTION blend = {AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
  return AlphaBlend(target, destination.left, destination.top,
                    static_cast<int>(destination_width),
                    static_cast<int>(destination_height), found->second->dc, 0, 0,
                    asset->width, asset->height, blend) != FALSE;
  } catch (...) {
    return false;
  }
}

bool DrawVanillaUiAsset(HDC target, const wchar_t* game_relative_path,
                        const UiAssetBox& destination, COLORREF tint,
                        BYTE opacity) {
  if (destination.w <= 0 || destination.h <= 0) return false;
  const int64_t right = static_cast<int64_t>(destination.x) + destination.w;
  const int64_t bottom = static_cast<int64_t>(destination.y) + destination.h;
  if (right < std::numeric_limits<LONG>::min() ||
      right > std::numeric_limits<LONG>::max() ||
      bottom < std::numeric_limits<LONG>::min() ||
      bottom > std::numeric_limits<LONG>::max())
    return false;
  const RECT rectangle = {destination.x, destination.y,
                          static_cast<LONG>(right), static_cast<LONG>(bottom)};
  return DrawVanillaUiAsset(target, game_relative_path, rectangle, tint,
                            opacity);
}

bool DrawVanillaUiAssetFit(HDC target, const wchar_t* game_relative_path,
                           const UiAssetBox& bounds, COLORREF tint,
                           BYTE opacity) {
  try {
    if (!target || !game_relative_path || bounds.w <= 0 || bounds.h <= 0)
      return false;
    std::wstring normalized;
    if (!NormalizeRelativePath(game_relative_path, &normalized)) return false;
    int source_width = 0;
    int source_height = 0;
    {
      CacheState& state = State();
      std::lock_guard<std::mutex> lock(state.mutex);
      if (!InitializeLocked(state, nullptr)) return false;
      const std::shared_ptr<Asset> asset = LoadAssetLocked(state, normalized);
      if (!asset || !asset->bgra.GuardsIntact() || asset->width <= 0 ||
          asset->height <= 0)
        return false;
      source_width = asset->width;
      source_height = asset->height;
    }
    const double scale = std::min(
        bounds.w / static_cast<double>(source_width),
        bounds.h / static_cast<double>(source_height));
    const int width = std::max(1, static_cast<int>(std::lround(
        source_width * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(
        source_height * scale)));
    const UiAssetBox destination = {
        bounds.x + (bounds.w - width) / 2,
        bounds.y + (bounds.h - height) / 2, width, height};
    return DrawVanillaUiAsset(target, game_relative_path, destination, tint,
                              opacity);
  } catch (...) {
    return false;
  }
}

bool DrawVanillaUiAssetContentFit(HDC target,
                                  const wchar_t* game_relative_path,
                                  const UiAssetBox& bounds, COLORREF tint,
                                  BYTE opacity) {
  try {
    if (!target || !game_relative_path || bounds.w <= 0 || bounds.h <= 0 ||
        opacity == 0)
      return false;
    std::wstring normalized;
    if (!NormalizeRelativePath(game_relative_path, &normalized)) return false;
    CacheState& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!InitializeLocked(state, nullptr)) return false;
    const std::shared_ptr<Asset> asset = LoadAssetLocked(state, normalized);
    if (!asset || !asset->bgra.GuardsIntact() ||
        asset->content_width <= 0 || asset->content_height <= 0)
      return false;

    const double scale = std::min(
        bounds.w / static_cast<double>(asset->content_width),
        bounds.h / static_cast<double>(asset->content_height));
    const int width = std::max(1, static_cast<int>(std::lround(
        asset->content_width * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(
        asset->content_height * scale)));
    const int destination_x = bounds.x + (bounds.w - width) / 2;
    const int destination_y = bounds.y + (bounds.h - height) / 2;

    const uint32_t tint_key = static_cast<uint32_t>(tint) & 0x00ffffffu;
    auto found = asset->tints.find(tint_key);
    if (found == asset->tints.end()) {
      if (asset->tints.size() >= kMaxTintsPerAsset)
        asset->tints.erase(asset->tints.begin());
      std::unique_ptr<TintDib> dib = CreateTintDib(*asset, tint);
      if (!dib) return false;
      found = asset->tints.emplace(tint_key, std::move(dib)).first;
    }
    const BLENDFUNCTION blend = {AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
    return AlphaBlend(target, destination_x, destination_y, width, height,
                      found->second->dc, asset->content_x, asset->content_y,
                      asset->content_width, asset->content_height,
                      blend) != FALSE;
  } catch (...) {
    return false;
  }
}

bool DrawVanillaUiAssetContentFitRotated(
    HDC target, const wchar_t* game_relative_path, const UiAssetBox& bounds,
    float angle_degrees, COLORREF tint, BYTE opacity) {
  if (!target || !std::isfinite(angle_degrees)) return false;
  const int saved = SaveDC(target);
  if (!saved || SetGraphicsMode(target, GM_ADVANCED) == 0) {
    if (saved) RestoreDC(target, saved);
    return false;
  }
  constexpr float kDegreesToRadians = 0.01745329251994329577f;
  const float angle = angle_degrees * kDegreesToRadians;
  const float cosine = std::cos(angle);
  const float sine = std::sin(angle);
  const float center_x = bounds.x + bounds.w * 0.5f;
  const float center_y = bounds.y + bounds.h * 0.5f;
  const XFORM transform = {
      cosine, sine, -sine, cosine,
      center_x - cosine * center_x + sine * center_y,
      center_y - sine * center_x - cosine * center_y};
  const bool transformed = SetWorldTransform(target, &transform) != FALSE;
  const bool drawn = transformed && DrawVanillaUiAssetContentFit(
      target, game_relative_path, bounds, tint, opacity);
  RestoreDC(target, saved);
  return drawn;
}

bool DrawVanillaUiAssetContentFramed(
    HDC target, const wchar_t* game_relative_path, const UiAssetBox& bounds,
    int family_content_width, int family_content_height, COLORREF tint,
    BYTE opacity) {
  try {
    if (!target || !game_relative_path || bounds.w <= 0 || bounds.h <= 0 ||
        family_content_width <= 0 || family_content_height <= 0 || opacity == 0)
      return false;
    std::wstring normalized;
    if (!NormalizeRelativePath(game_relative_path, &normalized)) return false;
    CacheState& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!InitializeLocked(state, nullptr)) return false;
    const std::shared_ptr<Asset> asset = LoadAssetLocked(state, normalized);
    if (!asset || !asset->bgra.GuardsIntact() || asset->content_width <= 0 ||
        asset->content_height <= 0)
      return false;

    const double scale = std::min(
        bounds.w / static_cast<double>(family_content_width),
        bounds.h / static_cast<double>(family_content_height));
    const int canvas_width = std::max(1, static_cast<int>(std::lround(
        asset->width * scale)));
    const int canvas_height = std::max(1, static_cast<int>(std::lround(
        asset->height * scale)));
    const double content_center_x =
        (asset->content_x + asset->content_width * 0.5) * scale;
    const double content_center_y =
        (asset->content_y + asset->content_height * 0.5) * scale;
    const int destination_x = static_cast<int>(std::lround(
        bounds.x + bounds.w * 0.5 - content_center_x));
    const int destination_y = static_cast<int>(std::lround(
        bounds.y + bounds.h * 0.5 - content_center_y));

    const uint32_t tint_key = static_cast<uint32_t>(tint) & 0x00ffffffu;
    auto found = asset->tints.find(tint_key);
    if (found == asset->tints.end()) {
      if (asset->tints.size() >= kMaxTintsPerAsset)
        asset->tints.erase(asset->tints.begin());
      std::unique_ptr<TintDib> dib = CreateTintDib(*asset, tint);
      if (!dib) return false;
      found = asset->tints.emplace(tint_key, std::move(dib)).first;
    }
    const int saved = SaveDC(target);
    if (!saved) return false;
    IntersectClipRect(target, bounds.x, bounds.y,
                      bounds.x + bounds.w, bounds.y + bounds.h);
    const BLENDFUNCTION blend = {AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
    const bool drawn = AlphaBlend(
        target, destination_x, destination_y, canvas_width, canvas_height,
        found->second->dc, 0, 0, asset->width, asset->height, blend) != FALSE;
    RestoreDC(target, saved);
    return drawn;
  } catch (...) {
    return false;
  }
}

bool DrawVanillaUiAssetPixelScaled(
    HDC target, const wchar_t* game_relative_path, int destination_x,
    int destination_y, float destination_pixels_per_source_pixel,
    COLORREF tint, BYTE opacity) {
  try {
    if (!target || !game_relative_path ||
        !std::isfinite(destination_pixels_per_source_pixel) ||
        destination_pixels_per_source_pixel <= 0.0f || opacity == 0)
      return false;
    std::wstring normalized;
    if (!NormalizeRelativePath(game_relative_path, &normalized)) return false;
    CacheState& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!InitializeLocked(state, nullptr)) return false;
    const std::shared_ptr<Asset> asset = LoadAssetLocked(state, normalized);
    if (!asset || !asset->bgra.GuardsIntact() || asset->width <= 0 ||
        asset->height <= 0)
      return false;
    const int width = std::max(1, static_cast<int>(std::lround(
        asset->width * destination_pixels_per_source_pixel)));
    const int height = std::max(1, static_cast<int>(std::lround(
        asset->height * destination_pixels_per_source_pixel)));
    const uint32_t tint_key = static_cast<uint32_t>(tint) & 0x00ffffffu;
    auto found = asset->tints.find(tint_key);
    if (found == asset->tints.end()) {
      if (asset->tints.size() >= kMaxTintsPerAsset)
        asset->tints.erase(asset->tints.begin());
      std::unique_ptr<TintDib> dib = CreateTintDib(*asset, tint);
      if (!dib) return false;
      found = asset->tints.emplace(tint_key, std::move(dib)).first;
    }
    const BLENDFUNCTION blend = {AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
    return AlphaBlend(target, destination_x, destination_y, width, height,
                      found->second->dc, 0, 0, asset->width, asset->height,
                      blend) != FALSE;
  } catch (...) {
    return false;
  }
}

bool DrawVanillaUiAssetRegion(HDC target,
                              const wchar_t* game_relative_path,
                              const UiAssetBox& destination,
                              float source_left, float source_top,
                              float source_right, float source_bottom,
                              COLORREF tint, BYTE opacity) {
  try {
    if (!target || !game_relative_path || destination.w <= 0 ||
        destination.h <= 0 || opacity == 0 || !std::isfinite(source_left) ||
        !std::isfinite(source_top) || !std::isfinite(source_right) ||
        !std::isfinite(source_bottom))
      return false;
    const float left = std::clamp(source_left, 0.0f, 1.0f);
    const float top = std::clamp(source_top, 0.0f, 1.0f);
    const float right = std::clamp(source_right, 0.0f, 1.0f);
    const float bottom = std::clamp(source_bottom, 0.0f, 1.0f);
    if (right <= left || bottom <= top) return false;

    std::wstring normalized;
    if (!NormalizeRelativePath(game_relative_path, &normalized)) return false;
    CacheState& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!InitializeLocked(state, nullptr)) return false;
    const std::shared_ptr<Asset> asset = LoadAssetLocked(state, normalized);
    if (!asset || !asset->bgra.GuardsIntact() || asset->width <= 0 ||
        asset->height <= 0)
      return false;

    const int source_x = std::clamp(
        static_cast<int>(std::floor(left * asset->width)), 0,
        asset->width - 1);
    const int source_y = std::clamp(
        static_cast<int>(std::floor(top * asset->height)), 0,
        asset->height - 1);
    const int source_end_x = std::clamp(
        static_cast<int>(std::ceil(right * asset->width)), source_x + 1,
        asset->width);
    const int source_end_y = std::clamp(
        static_cast<int>(std::ceil(bottom * asset->height)), source_y + 1,
        asset->height);

    const uint32_t tint_key = static_cast<uint32_t>(tint) & 0x00ffffffu;
    auto found = asset->tints.find(tint_key);
    if (found == asset->tints.end()) {
      if (asset->tints.size() >= kMaxTintsPerAsset)
        asset->tints.erase(asset->tints.begin());
      std::unique_ptr<TintDib> dib = CreateTintDib(*asset, tint);
      if (!dib) return false;
      found = asset->tints.emplace(tint_key, std::move(dib)).first;
    }
    const BLENDFUNCTION blend = {AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
    return AlphaBlend(target, destination.x, destination.y,
                      destination.w, destination.h, found->second->dc,
                      source_x, source_y, source_end_x - source_x,
                      source_end_y - source_y, blend) != FALSE;
  } catch (...) {
    return false;
  }
}

VanillaUiAssetCacheStats GetVanillaUiAssetCacheStats() {
  CacheState& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  VanillaUiAssetCacheStats result = {};
  result.file_reads = state.file_reads;
  result.dds_decodes = state.dds_decodes;
  result.cached_bytes = state.cached_asset_bytes;
  for (const auto& cached : state.assets) {
    if (cached.second) {
      ++result.cached_assets;
      result.cached_tints += cached.second->tints.size();
    } else {
      ++result.cached_misses;
    }
  }
  return result;
}

void ClearVanillaUiAssetCache() {
  CacheState& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.assets.clear();
  state.cached_asset_bytes = 0;
  state.access_clock = 0;
}

void ShutdownVanillaUiAssets() {
  CacheState& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.assets.clear();
  state.cached_asset_bytes = 0;
  state.access_clock = 0;
  state.archives.clear();
  state.game_directory.clear();
  state.data_directory.clear();
  state.initialized = false;
}

}  // namespace fo3ds

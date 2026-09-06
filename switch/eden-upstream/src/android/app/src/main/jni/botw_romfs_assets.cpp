#include "jni/botw_romfs_assets.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <zlib.h>

#include <bc_decoder.h>

#include "common/stb.h"
#include "common/zstd_compression.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/filesystem/romfs_controller.h"
#include "jni/native.h"
#include "jni/smo_state_reader.h"
#include "video_core/textures/astc.h"
#include "video_core/textures/decoders.h"

namespace DualScreenRomfsAssets {
namespace {

constexpr u64 BotwTitleId = 0x01007EF00011E000ULL;
constexpr u64 Mk8dTitleId = 0x0100152000022000ULL;

constexpr u64 SshdTitleId = 0x01002DA013484000ULL;
constexpr u64 XcxTitleId = 0x0100453019AA8000ULL;
constexpr std::size_t MaxSourceSize = 4 * 1024 * 1024;
constexpr std::size_t MaxArchiveSize = 64 * 1024 * 1024;
constexpr std::size_t MaxDecodedSourceSize = 64 * 1024 * 1024;
constexpr std::size_t MaxPngSize = 8 * 1024 * 1024;
constexpr u32 MaxTextureDimension = 2048;
constexpr u64 MaxTexturePixels = 4 * 1024 * 1024;
constexpr auto AssetRetryDelay = std::chrono::seconds{5};

std::mutex romfs_mutex;
std::mutex asset_mutex;
FileSys::VirtualDir romfs_root;
u64 romfs_title_id{};
std::unordered_map<std::string, std::vector<u8>> botw_common_cache;
std::vector<u8> botw_map_cache;
std::unordered_map<std::string, std::vector<u8>> mk8d_character_cache;
std::unordered_map<std::string, std::vector<u8>> mk8d_item_cache;
std::unordered_map<std::string, std::vector<u8>> mk8d_common_cache;

std::unordered_map<std::string, std::vector<u8>> sshd_asset_cache;
std::unordered_map<std::string, std::shared_ptr<std::vector<u8>>> sshd_archive_cache;
std::unordered_map<std::string, std::vector<u8>> xcx_asset_cache;
std::unordered_map<std::string, std::vector<u8>> smo_asset_cache;
std::optional<std::vector<u8>> smo_map_archive;
std::unordered_map<std::string, std::vector<u8>> smo_icon_archives;
std::unordered_map<std::string, std::vector<u8>> smo_icon_cache;
std::optional<std::vector<u8>> smo_stage_messages;
std::shared_ptr<std::vector<u8>> xcx_common_bdat;
std::shared_ptr<std::vector<u8>> xcx_message_bdat;
std::shared_ptr<std::vector<u8>> mk8d_character_archive;
std::shared_ptr<std::vector<u8>> mk8d_item_archive;
std::shared_ptr<std::vector<u8>> botw_common_archive;
bool botw_common_loaded{};
std::chrono::steady_clock::time_point botw_common_retry_after{};
std::chrono::steady_clock::time_point botw_map_retry_after{};
bool mk8d_character_failed{};
bool mk8d_item_failed{};
bool mk8d_common_loaded{};

struct XcxArchiveRange {
    u64 offset{};
    u32 disk_size{};
    u32 expanded_size{};
};

std::unordered_map<u64, XcxArchiveRange> xcx_archive_index;
FileSys::VirtualFile xcx_archive_store;
bool xcx_archive_loaded{};
std::chrono::steady_clock::time_point xcx_archive_retry_after{};



struct FormatInfo {
    u32 block_width;
    u32 block_height;
    u32 bytes_per_block;
    u32 channels;
};

struct TextureInfo {
    u32 width;
    u32 height;
    u32 format;
    u32 image_size;
    u32 array_length;
    u32 texture_layout;
    u32 channel_map;
    u16 tile_mode;
    u64 image_offset;
};

struct PngOutput {
    std::vector<u8> bytes;
    bool failed{};
};

struct DecodedTexture {
    u32 width;
    u32 height;
    std::vector<u8> rgba;
};

u16 ReadU16(std::span<const u8> data, std::size_t offset, bool* valid = nullptr) {
    const bool in_bounds = offset <= data.size() && data.size() - offset >= 2;
    if (valid) {
        *valid = in_bounds;
    }
    if (!in_bounds) {
        return 0;
    }
    return static_cast<u16>(data[offset]) | static_cast<u16>(data[offset + 1] << 8);
}

u32 ReadU32(std::span<const u8> data, std::size_t offset, bool* valid = nullptr) {
    const bool in_bounds = offset <= data.size() && data.size() - offset >= 4;
    if (valid) {
        *valid = in_bounds;
    }
    if (!in_bounds) {
        return 0;
    }
    return static_cast<u32>(data[offset]) | static_cast<u32>(data[offset + 1]) << 8 |
           static_cast<u32>(data[offset + 2]) << 16 | static_cast<u32>(data[offset + 3]) << 24;
}

u64 ReadU64(std::span<const u8> data, std::size_t offset, bool* valid = nullptr) {
    const bool in_bounds = offset <= data.size() && data.size() - offset >= 8;
    if (valid) {
        *valid = in_bounds;
    }
    if (!in_bounds) {
        return 0;
    }
    u64 value{};
    for (u32 index = 0; index < 8; ++index) {
        value |= static_cast<u64>(data[offset + index]) << (index * 8);
    }
    return value;
}

u32 DivCeil(u32 value, u32 divisor) {
    return (value + divisor - 1) / divisor;
}

std::size_t AlignUp(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

bool HasMagic(std::span<const u8> data, std::string_view magic) {
    return data.size() >= magic.size() &&
           std::memcmp(data.data(), magic.data(), magic.size()) == 0;
}

u16 ReadEndianU16(std::span<const u8> data, std::size_t offset, bool big_endian,
                  bool* valid = nullptr) {
    const bool in_bounds = offset <= data.size() && data.size() - offset >= 2;
    if (valid) {
        *valid = in_bounds;
    }
    if (!in_bounds) {
        return 0;
    }
    if (big_endian) {
        return static_cast<u16>(data[offset]) << 8 | static_cast<u16>(data[offset + 1]);
    }
    return ReadU16(data, offset);
}

u32 ReadEndianU32(std::span<const u8> data, std::size_t offset, bool big_endian,
                  bool* valid = nullptr) {
    const bool in_bounds = offset <= data.size() && data.size() - offset >= 4;
    if (valid) {
        *valid = in_bounds;
    }
    if (!in_bounds) {
        return 0;
    }
    if (big_endian) {
        return static_cast<u32>(data[offset]) << 24 | static_cast<u32>(data[offset + 1]) << 16 |
               static_cast<u32>(data[offset + 2]) << 8 | static_cast<u32>(data[offset + 3]);
    }
    return ReadU32(data, offset);
}

std::optional<std::vector<u8>> ExtractSarcFile(std::span<const u8> sarc,
                                               std::string_view requested_name) {
    if (sarc.size() < 0x20 || !HasMagic(sarc, "SARC")) {
        return std::nullopt;
    }
    const bool big_endian = sarc[6] == 0xFE && sarc[7] == 0xFF;
    if (!big_endian && !(sarc[6] == 0xFF && sarc[7] == 0xFE)) {
        return std::nullopt;
    }
    bool valid{};
    const u16 header_size = ReadEndianU16(sarc, 4, big_endian, &valid);
    const u32 data_offset = ReadEndianU32(sarc, 0x0C, big_endian, &valid);
    if (!valid || header_size < 0x14 || header_size > sarc.size() || data_offset > sarc.size() ||
        header_size + 0x0C > sarc.size() ||
        !HasMagic(sarc.subspan(header_size), "SFAT")) {
        return std::nullopt;
    }
    const u16 node_count = ReadEndianU16(sarc, header_size + 6, big_endian, &valid);
    const std::size_t nodes_offset = header_size + 0x0C;
    if (!valid || node_count > 8192 || static_cast<std::size_t>(node_count) * 0x10 >
                                           sarc.size() - nodes_offset) {
        return std::nullopt;
    }
    const std::size_t sfnt_offset = nodes_offset + static_cast<std::size_t>(node_count) * 0x10;
    if (sfnt_offset + 8 > sarc.size() || !HasMagic(sarc.subspan(sfnt_offset), "SFNT")) {
        return std::nullopt;
    }
    const u16 sfnt_header_size = ReadEndianU16(sarc, sfnt_offset + 4, big_endian, &valid);
    const std::size_t names_offset = sfnt_offset + sfnt_header_size;
    if (!valid || sfnt_header_size < 8 || names_offset > sarc.size()) {
        return std::nullopt;
    }
    for (u16 index = 0; index < node_count; ++index) {
        const std::size_t node = nodes_offset + static_cast<std::size_t>(index) * 0x10;
        const u32 attributes = ReadEndianU32(sarc, node + 4, big_endian, &valid);
        const u32 start = ReadEndianU32(sarc, node + 8, big_endian, &valid);
        const u32 end = ReadEndianU32(sarc, node + 0x0C, big_endian, &valid);
        if (!valid || (attributes & 0x01000000) == 0) {
            continue;
        }
        const std::size_t name_offset =
            names_offset + static_cast<std::size_t>(attributes & 0x00FFFFFF) * 4;
        if (name_offset >= sarc.size()) {
            continue;
        }
        const auto terminator = std::find(sarc.begin() + name_offset, sarc.end(), 0);
        if (terminator == sarc.end()) {
            continue;
        }
        const std::string_view name(reinterpret_cast<const char*>(sarc.data() + name_offset),
                                    static_cast<std::size_t>(terminator - sarc.begin()) - name_offset);
        if (name != requested_name || start > end || data_offset > sarc.size() ||
            end > sarc.size() - data_offset) {
            continue;
        }
        return std::vector<u8>(sarc.begin() + data_offset + start,
                               sarc.begin() + data_offset + end);
    }
    return std::nullopt;
}

bool EqualAsciiInsensitive(std::string_view left, std::string_view right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}

std::optional<std::vector<u8>> DecompressYaz0(std::span<const u8> source) {
    if (!HasMagic(source, "Yaz0")) {
        if (source.size() > MaxDecodedSourceSize) {
            return std::nullopt;
        }
        return std::vector<u8>(source.begin(), source.end());
    }
    if (source.size() < 16) {
        return std::nullopt;
    }
    const u32 decoded_size = static_cast<u32>(source[4]) << 24 |
                             static_cast<u32>(source[5]) << 16 |
                             static_cast<u32>(source[6]) << 8 | static_cast<u32>(source[7]);
    if (decoded_size == 0 || decoded_size > MaxDecodedSourceSize) {
        return std::nullopt;
    }
    std::vector<u8> output(decoded_size);
    std::size_t input_offset = 16;
    std::size_t output_offset = 0;
    u8 code{};
    u8 mask{};
    while (output_offset < output.size()) {
        if (mask == 0) {
            if (input_offset >= source.size()) {
                return std::nullopt;
            }
            code = source[input_offset++];
            mask = 0x80;
        }
        if ((code & mask) != 0) {
            if (input_offset >= source.size()) {
                return std::nullopt;
            }
            output[output_offset++] = source[input_offset++];
        } else {
            if (input_offset + 1 >= source.size()) {
                return std::nullopt;
            }
            const u8 first = source[input_offset++];
            const u8 second = source[input_offset++];
            const std::size_t distance = (static_cast<std::size_t>(first & 0x0F) << 8) | second;
            if (distance + 1 > output_offset) {
                return std::nullopt;
            }
            std::size_t length = first >> 4;
            if (length == 0) {
                if (input_offset >= source.size()) {
                    return std::nullopt;
                }
                length = static_cast<std::size_t>(source[input_offset++]) + 0x12;
            } else {
                length += 2;
            }
            std::size_t copy_offset = output_offset - distance - 1;
            while (length-- > 0 && output_offset < output.size()) {
                output[output_offset++] = output[copy_offset++];
            }
        }
        mask >>= 1;
    }
    return output;
}

std::optional<std::string> ReadBntxString(std::span<const u8> bntx, u64 offset) {
    if (offset > bntx.size() || bntx.size() - static_cast<std::size_t>(offset) < 2) {
        return std::nullopt;
    }
    bool valid{};
    const u16 length = ReadU16(bntx, static_cast<std::size_t>(offset), &valid);
    const std::size_t start = static_cast<std::size_t>(offset) + 2;
    if (!valid || start > bntx.size() || length > bntx.size() - start) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(bntx.data() + start), length);
}

std::optional<TextureInfo> ParseTexture(std::span<const u8> bntx, u64 texture_offset) {
    if (texture_offset > bntx.size() || bntx.size() - static_cast<std::size_t>(texture_offset) < 0xA0) {
        return std::nullopt;
    }
    const std::size_t offset = static_cast<std::size_t>(texture_offset);
    if (!HasMagic(bntx.subspan(offset), "BRTI")) {
        return std::nullopt;
    }
    bool valid{};
    TextureInfo result{};
    result.tile_mode = ReadU16(bntx, offset + 0x12, &valid);
    if (!valid) {
        return std::nullopt;
    }
    result.format = ReadU32(bntx, offset + 0x1C, &valid);
    result.width = ReadU32(bntx, offset + 0x24, &valid);
    result.height = ReadU32(bntx, offset + 0x28, &valid);
    result.array_length = ReadU32(bntx, offset + 0x30, &valid);
    result.texture_layout = ReadU32(bntx, offset + 0x34, &valid);
    result.image_size = ReadU32(bntx, offset + 0x50, &valid);
    result.channel_map = ReadU32(bntx, offset + 0x58, &valid);
    const u64 mip_offsets = ReadU64(bntx, offset + 0x70, &valid);
    if (!valid || mip_offsets > bntx.size() || bntx.size() - static_cast<std::size_t>(mip_offsets) < 8) {
        return std::nullopt;
    }
    result.image_offset = ReadU64(bntx, static_cast<std::size_t>(mip_offsets), &valid);
    if (!valid || result.width == 0 || result.height == 0 || result.array_length == 0 ||
        result.image_size == 0 || result.width > MaxTextureDimension ||
        result.height > MaxTextureDimension ||
        static_cast<u64>(result.width) * result.height > MaxTexturePixels ||
        result.image_offset >= bntx.size()) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::pair<TextureInfo, std::span<const u8>>> FindTexture(
    std::span<const u8> source, std::string_view requested_name, bool allow_first = false) {
    const std::array<u8, 4> magic{'B', 'N', 'T', 'X'};
    const auto found = std::search(source.begin(), source.end(), magic.begin(), magic.end());
    if (found == source.end()) {
        return std::nullopt;
    }
    const std::size_t bntx_offset = static_cast<std::size_t>(found - source.begin());
    std::span<const u8> bntx = source.subspan(bntx_offset);
    if (bntx.size() < 0x30) {
        return std::nullopt;
    }
    const u32 declared_size = ReadU32(bntx, 0x1C);
    if (declared_size >= 0x30 && declared_size <= bntx.size()) {
        bntx = bntx.first(declared_size);
    }
    const u32 texture_count = ReadU32(bntx, 0x24);
    const u64 texture_array = ReadU64(bntx, 0x28);
    if (texture_count == 0 || texture_count > 4096 || texture_array > bntx.size() ||
        static_cast<u64>(texture_count) * 8 > bntx.size() - texture_array) {
        return std::nullopt;
    }
    std::optional<TextureInfo> first_texture;
    for (u32 index = 0; index < texture_count; ++index) {
        const u64 texture_offset = ReadU64(bntx, static_cast<std::size_t>(texture_array) + index * 8);
        const auto texture = ParseTexture(bntx, texture_offset);
        if (!texture) {
            continue;
        }
        if (!first_texture) {
            first_texture = texture;
        }
        const u64 name_offset = ReadU64(bntx, static_cast<std::size_t>(texture_offset) + 0x60);
        const auto name = ReadBntxString(bntx, name_offset);
        if (name && EqualAsciiInsensitive(*name, requested_name)) {
            return std::pair{*texture, bntx};
        }
    }
    if (!first_texture || (!allow_first && !requested_name.empty())) {
        return std::nullopt;
    }
    return std::pair{*first_texture, bntx};
}

std::optional<FormatInfo> GetFormatInfo(u32 format) {
    switch (format >> 8) {
    case 2:
        return FormatInfo{1, 1, 1, 1};
    case 9:
        return FormatInfo{1, 1, 2, 2};
    case 11:
    case 12:
        return FormatInfo{1, 1, 4, 4};
    case 26:
        return FormatInfo{4, 4, 8, 4};
    case 27:
    case 28:
        return FormatInfo{4, 4, 16, 4};
    case 29:
        return FormatInfo{4, 4, 8, 1};
    case 30:
        return FormatInfo{4, 4, 16, 2};
    case 32:
        return FormatInfo{4, 4, 16, 4};
    case 45:
        return FormatInfo{4, 4, 16, 4};
    case 46:
        return FormatInfo{5, 4, 16, 4};
    case 47:
        return FormatInfo{5, 5, 16, 4};
    case 48:
        return FormatInfo{6, 5, 16, 4};
    case 49:
        return FormatInfo{6, 6, 16, 4};
    case 50:
        return FormatInfo{8, 5, 16, 4};
    case 51:
        return FormatInfo{8, 6, 16, 4};
    case 52:
        return FormatInfo{8, 8, 16, 4};
    case 53:
        return FormatInfo{10, 5, 16, 4};
    case 54:
        return FormatInfo{10, 6, 16, 4};
    case 55:
        return FormatInfo{10, 8, 16, 4};
    case 56:
        return FormatInfo{10, 10, 16, 4};
    case 57:
        return FormatInfo{12, 10, 16, 4};
    case 58:
        return FormatInfo{12, 12, 16, 4};
    default:
        return std::nullopt;
    }
}

std::optional<std::vector<u8>> Unswizzle(const TextureInfo& texture,
                                         std::span<const u8> bntx,
                                         const FormatInfo& format) {
    const u32 blocks_x = DivCeil(texture.width, format.block_width);
    const u32 blocks_y = DivCeil(texture.height, format.block_height);
    const std::size_t linear_size = static_cast<std::size_t>(blocks_x) * blocks_y *
                                    format.bytes_per_block;
    const std::size_t array_size = texture.image_size / texture.array_length;
    if (linear_size == 0 || texture.image_offset > bntx.size() ||
        array_size > bntx.size() - static_cast<std::size_t>(texture.image_offset)) {
        return std::nullopt;
    }
    const auto source = bntx.subspan(static_cast<std::size_t>(texture.image_offset), array_size);
    std::vector<u8> linear(linear_size);
    if (texture.tile_mode == 0) {
        const u32 block_height = texture.texture_layout & 7;
        const std::size_t required = Tegra::Texture::CalculateSize(
            true, format.bytes_per_block, blocks_x, blocks_y, 1, block_height, 0);
        if (required > source.size()) {
            return std::nullopt;
        }
        Tegra::Texture::UnswizzleTexture(linear, source.first(required), format.bytes_per_block,
                                         blocks_x, blocks_y, 1, block_height, 0);
        return linear;
    }
    if (texture.tile_mode != 1) {
        return std::nullopt;
    }
    const std::size_t row_size = static_cast<std::size_t>(blocks_x) * format.bytes_per_block;
    std::size_t pitch = AlignUp(row_size, 32);
    if (pitch * blocks_y > source.size()) {
        pitch = row_size;
    }
    if (pitch * blocks_y > source.size()) {
        return std::nullopt;
    }
    for (u32 row = 0; row < blocks_y; ++row) {
        std::memcpy(linear.data() + static_cast<std::size_t>(row) * row_size,
                    source.data() + static_cast<std::size_t>(row) * pitch, row_size);
    }
    return linear;
}

std::optional<std::vector<u8>> DecodeChannels(const TextureInfo& texture,
                                               const FormatInfo& format,
                                               std::span<const u8> blocks) {
    const u32 format_code = texture.format >> 8;
    std::vector<u8> channels(static_cast<std::size_t>(texture.width) * texture.height *
                             format.channels);
    if (format_code == 2 || format_code == 9 || format_code == 11) {
        if (blocks.size() < channels.size()) {
            return std::nullopt;
        }
        std::copy_n(blocks.begin(), channels.size(), channels.begin());
        return channels;
    }
    if (format_code == 12) {
        if (blocks.size() < channels.size()) {
            return std::nullopt;
        }
        for (std::size_t pixel = 0; pixel < channels.size(); pixel += 4) {
            channels[pixel] = blocks[pixel + 2];
            channels[pixel + 1] = blocks[pixel + 1];
            channels[pixel + 2] = blocks[pixel];
            channels[pixel + 3] = blocks[pixel + 3];
        }
        return channels;
    }
    if (format_code >= 45 && format_code <= 58) {
        Tegra::Texture::ASTC::Decompress(blocks, texture.width, texture.height, 1,
                                         format.block_width, format.block_height, channels);
        return channels;
    }
    const u32 blocks_x = DivCeil(texture.width, 4);
    const u32 blocks_y = DivCeil(texture.height, 4);
    const bool signed_format = (texture.format & 0xFF) == 2;
    for (u32 block_y = 0; block_y < blocks_y; ++block_y) {
        for (u32 block_x = 0; block_x < blocks_x; ++block_x) {
            const std::size_t source_offset =
                (static_cast<std::size_t>(block_y) * blocks_x + block_x) *
                format.bytes_per_block;
            if (source_offset + format.bytes_per_block > blocks.size()) {
                return std::nullopt;
            }
            const u8* source = blocks.data() + source_offset;
            u8* destination = channels.data() +
                              (static_cast<std::size_t>(block_y) * 4 * texture.width +
                               block_x * 4) * format.channels;
            const u32 x = block_x * 4;
            const u32 y = block_y * 4;
            switch (format_code) {
            case 26:
                bcn::DecodeBc1(source, destination, x, y, texture.width, texture.height);
                break;
            case 27:
                bcn::DecodeBc2(source, destination, x, y, texture.width, texture.height);
                break;
            case 28:
                bcn::DecodeBc3(source, destination, x, y, texture.width, texture.height);
                break;
            case 29:
                bcn::DecodeBc4(source, destination, x, y, texture.width, texture.height,
                               signed_format);
                break;
            case 30:
                bcn::DecodeBc5(source, destination, x, y, texture.width, texture.height,
                               signed_format);
                break;
            case 32:
                bcn::DecodeBc7(source, destination, x, y, texture.width, texture.height);
                break;
            default:
                return std::nullopt;
            }
        }
    }
    return channels;
}

u8 SelectChannel(const u8* source, u32 channel_count, u8 selector) {
    if (selector == 0) {
        return 0;
    }
    if (selector == 1) {
        return 255;
    }
    const u32 index = selector - 2;
    return index < channel_count ? source[index] : 0;
}

std::optional<std::vector<u8>> ConvertToRgba(const TextureInfo& texture,
                                             const FormatInfo& format,
                                             std::span<const u8> channels) {
    const std::size_t pixels = static_cast<std::size_t>(texture.width) * texture.height;
    if (channels.size() < pixels * format.channels) {
        return std::nullopt;
    }
    std::vector<u8> rgba(pixels * 4);
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        const u8* source = channels.data() + pixel * format.channels;
        for (u32 channel = 0; channel < 4; ++channel) {
            const u8 selector = static_cast<u8>(texture.channel_map >> (channel * 8));
            rgba[pixel * 4 + channel] = SelectChannel(source, format.channels, selector);
        }
    }
    return rgba;
}

void WritePngBytes(void* context, void* data, int size) {
    auto& output = *static_cast<PngOutput*>(context);
    if (output.failed || size <= 0 || static_cast<std::size_t>(size) > MaxPngSize - output.bytes.size()) {
        output.failed = true;
        return;
    }
    try {
        const auto* bytes = static_cast<const u8*>(data);
        output.bytes.insert(output.bytes.end(), bytes, bytes + size);
    } catch (...) {
        output.failed = true;
    }
}

std::vector<u8> EncodePng(const TextureInfo& texture, std::span<const u8> rgba) {
    if (texture.width > static_cast<u32>(std::numeric_limits<int>::max()) ||
        texture.height > static_cast<u32>(std::numeric_limits<int>::max())) {
        return {};
    }
    PngOutput output;
    output.bytes.reserve(std::min<std::size_t>(rgba.size(), 256 * 1024));
    const int result = stbi_write_png_to_func(
        WritePngBytes, &output, static_cast<int>(texture.width), static_cast<int>(texture.height),
        4, rgba.data(), static_cast<int>(texture.width * 4));
    if (result == 0 || output.failed || output.bytes.empty()) {
        return {};
    }
    return output.bytes;
}

bool IsSafeComponent(std::string_view value) {
    return !value.empty() && value.size() <= 128 &&
           std::all_of(value.begin(), value.end(), [](char character) {
               return std::isalnum(static_cast<unsigned char>(character)) || character == '_' ||
                      character == '-' || character == '.';
           });
}

std::optional<std::string> LogicalComponent(std::string_view path, std::string_view prefix,
                                            std::string_view suffix) {
    if (!path.starts_with(prefix) || !path.ends_with(suffix) ||
        path.size() <= prefix.size() + suffix.size()) {
        return std::nullopt;
    }
    const auto value = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
    if (!IsSafeComponent(value)) {
        return std::nullopt;
    }
    return std::string(value);
}

std::optional<DecodedTexture> DecodeTextureRgba(std::span<const u8> source,
                                                std::string_view texture_name,
                                                bool allow_first = false) {
    const auto texture = FindTexture(source, texture_name, allow_first);
    if (!texture) {
        return std::nullopt;
    }
    const auto format = GetFormatInfo(texture->first.format);
    if (!format) {
        return std::nullopt;
    }
    const auto blocks = Unswizzle(texture->first, texture->second, *format);
    if (!blocks) {
        return std::nullopt;
    }
    const auto channels = DecodeChannels(texture->first, *format, *blocks);
    if (!channels) {
        return std::nullopt;
    }
    const auto rgba = ConvertToRgba(texture->first, *format, *channels);
    if (!rgba) {
        return std::nullopt;
    }
    return DecodedTexture{texture->first.width, texture->first.height, std::move(*rgba)};
}

std::vector<u8> DecodeTexturePng(std::span<const u8> source, std::string_view texture_name,
                                 bool allow_first = false) {
    const auto texture = DecodeTextureRgba(source, texture_name, allow_first);
    if (!texture) {
        return {};
    }
    TextureInfo descriptor{};
    descriptor.width = texture->width;
    descriptor.height = texture->height;
    return EncodePng(descriptor, texture->rgba);
}

FileSys::VirtualDir GetRomfsRoot(EmulationSession& session, u64 expected_title_id) {
    std::scoped_lock lock(romfs_mutex);
    if (romfs_root && romfs_title_id == expected_title_id) {
        return romfs_root;
    }
    romfs_root.reset();
    romfs_title_id = 0;
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != expected_title_id) {
        return nullptr;
    }
    const auto* process = system.ApplicationProcess();
    if (!process) {
        return nullptr;
    }
    u64 program_id{};
    std::shared_ptr<Service::FileSystem::SaveDataController> save_data_controller;
    std::shared_ptr<Service::FileSystem::RomFsController> romfs_controller;
    const auto result = system.GetFileSystemController().OpenProcess(
        &program_id, &save_data_controller, &romfs_controller, process->GetProcessId());
    if (!result.IsSuccess() || program_id != expected_title_id || !romfs_controller) {
        return nullptr;
    }
    const auto current_romfs = romfs_controller->OpenRomFSCurrentProcess();
    if (!current_romfs) {
        return nullptr;
    }
    romfs_root = FileSys::ExtractRomFS(current_romfs);
    if (romfs_root) {
        romfs_title_id = expected_title_id;
    }
    return romfs_root;
}

std::optional<std::vector<u8>> ReadRomfsFile(const FileSys::VirtualDir& root,
                                             std::string_view path, std::size_t maximum_size) {
    if (!root) {
        return std::nullopt;
    }
    const auto file = root->GetFileRelative(std::string(path));
    if (!file || file->GetSize() == 0 || file->GetSize() > maximum_size) {
        return std::nullopt;
    }
    auto bytes = file->ReadAllBytes();
    if (bytes.size() != file->GetSize()) {
        return std::nullopt;
    }
    return bytes;
}

u16 ReadBe16(std::span<const u8> data, std::size_t offset, bool* valid = nullptr) {
    const bool in_bounds = offset <= data.size() && data.size() - offset >= 2;
    if (valid) {
        *valid = in_bounds;
    }
    if (!in_bounds) {
        return 0;
    }
    return static_cast<u16>(data[offset]) << 8 | static_cast<u16>(data[offset + 1]);
}

u32 ReadBe32(std::span<const u8> data, std::size_t offset, bool* valid = nullptr) {
    const bool in_bounds = offset <= data.size() && data.size() - offset >= 4;
    if (valid) {
        *valid = in_bounds;
    }
    if (!in_bounds) {
        return 0;
    }
    return static_cast<u32>(data[offset]) << 24 | static_cast<u32>(data[offset + 1]) << 16 |
           static_cast<u32>(data[offset + 2]) << 8 | static_cast<u32>(data[offset + 3]);
}

std::optional<std::vector<u8>> ExtractU8File(std::span<const u8> archive,
                                             std::string_view requested_path) {
    if (archive.size() < 0x20 || ReadBe32(archive, 0) != 0x55AA382D || requested_path.empty() ||
        requested_path.starts_with('/') || requested_path.find("..") != std::string_view::npos) {
        return std::nullopt;
    }
    bool valid{};
    const u32 root_offset = ReadBe32(archive, 4, &valid);
    if (!valid || root_offset > archive.size() || archive.size() - root_offset < 12) {
        return std::nullopt;
    }
    const u32 root_type_name = ReadBe32(archive, root_offset, &valid);
    const u32 node_count = ReadBe32(archive, root_offset + 8, &valid);
    if (!valid || (root_type_name >> 24) != 1 || node_count == 0 || node_count > 65536 ||
        static_cast<std::size_t>(node_count) * 12 > archive.size() - root_offset) {
        return std::nullopt;
    }
    const std::size_t strings_offset = root_offset + static_cast<std::size_t>(node_count) * 12;
    if (strings_offset >= archive.size()) {
        return std::nullopt;
    }

    struct DirectoryFrame {
        u32 end_index;
        std::string path;
    };
    std::vector<DirectoryFrame> directories{{node_count, {}}};
    for (u32 index = 1; index < node_count; ++index) {
        while (!directories.empty() && index >= directories.back().end_index) {
            directories.pop_back();
        }
        if (directories.empty()) {
            return std::nullopt;
        }
        const std::size_t node = root_offset + static_cast<std::size_t>(index) * 12;
        const u32 type_name = ReadBe32(archive, node, &valid);
        const u32 data_offset = ReadBe32(archive, node + 4, &valid);
        const u32 size_or_end = ReadBe32(archive, node + 8, &valid);
        const u32 name_offset = type_name & 0x00FFFFFF;
        if (!valid || name_offset >= archive.size() - strings_offset) {
            return std::nullopt;
        }
        const std::size_t name_start = strings_offset + name_offset;
        const auto name_end = std::find(archive.begin() + name_start, archive.end(), 0);
        if (name_end == archive.end()) {
            return std::nullopt;
        }
        const std::string_view name(
            reinterpret_cast<const char*>(archive.data() + name_start),
            static_cast<std::size_t>(name_end - (archive.begin() + name_start)));
        if (name.empty() || name == "." || name == ".." || name.find('/') != std::string_view::npos) {
            return std::nullopt;
        }
        std::string path = directories.back().path;
        if (!path.empty()) {
            path += '/';
        }
        path += name;
        if ((type_name >> 24) == 1) {
            if (size_or_end <= index || size_or_end > directories.back().end_index) {
                return std::nullopt;
            }
            directories.push_back({size_or_end, std::move(path)});
            continue;
        }
        if ((type_name >> 24) != 0 || data_offset > archive.size() ||
            size_or_end > archive.size() - data_offset) {
            return std::nullopt;
        }
        if (path == requested_path) {
            return std::vector<u8>(archive.begin() + data_offset,
                                   archive.begin() + data_offset + size_or_end);
        }
    }
    return std::nullopt;
}

u8 Expand4(u8 value) {
    return static_cast<u8>((value << 4) | value);
}

u8 Expand3(u8 value) {
    return static_cast<u8>((value << 5) | (value << 2) | (value >> 1));
}

u8 Expand5(u8 value) {
    return static_cast<u8>((value << 3) | (value >> 2));
}

u8 Expand6(u8 value) {
    return static_cast<u8>((value << 2) | (value >> 4));
}

std::vector<u8> DecodeTplPng(std::span<const u8> tpl) {
    bool valid{};
    if (tpl.size() < 0x20 || ReadBe32(tpl, 0, &valid) != 0x0020AF30 || !valid) {
        return {};
    }
    const u32 count = ReadBe32(tpl, 4, &valid);
    const u32 table = ReadBe32(tpl, 8, &valid);
    if (!valid || count == 0 || count > 16 || table > tpl.size() || tpl.size() - table < 8) {
        return {};
    }
    const u32 header = ReadBe32(tpl, table, &valid);
    if (!valid || header > tpl.size() || tpl.size() - header < 12) {
        return {};
    }
    const u32 height = ReadBe16(tpl, header, &valid);
    const u32 width = ReadBe16(tpl, header + 2, &valid);
    const u32 format = ReadBe32(tpl, header + 4, &valid);
    const u32 image_offset = ReadBe32(tpl, header + 8, &valid);
    if (!valid || width == 0 || height == 0 || width > 4096 || height > 4096 ||
        static_cast<u64>(width) * height > MaxTexturePixels || image_offset >= tpl.size() ||
        format > 6) {
        return {};
    }

    u32 tile_width{};
    u32 tile_height{};
    u32 tile_bytes{};
    switch (format) {
    case 0:
        tile_width = 8;
        tile_height = 8;
        tile_bytes = 32;
        break;
    case 1:
    case 2:
        tile_width = 8;
        tile_height = 4;
        tile_bytes = 32;
        break;
    case 3:
    case 4:
    case 5:
        tile_width = 4;
        tile_height = 4;
        tile_bytes = 32;
        break;
    case 6:
        tile_width = 4;
        tile_height = 4;
        tile_bytes = 64;
        break;
    default:
        return {};
    }
    const u64 tile_count = static_cast<u64>(DivCeil(width, tile_width)) *
                           DivCeil(height, tile_height);
    if (tile_count * tile_bytes > tpl.size() - image_offset) {
        return {};
    }
    std::vector<u8> rgba(static_cast<std::size_t>(width) * height * 4);
    std::size_t source = image_offset;
    for (u32 tile_y = 0; tile_y < height; tile_y += tile_height) {
        for (u32 tile_x = 0; tile_x < width; tile_x += tile_width) {
            for (u32 y = 0; y < tile_height; ++y) {
                for (u32 x = 0; x < tile_width; ++x) {
                    const u32 pixel = y * tile_width + x;
                    u8 red{}, green{}, blue{}, alpha{255};
                    if (format == 0) {
                        const u8 packed = tpl[source + pixel / 2];
                        const u8 intensity = Expand4(pixel & 1 ? packed & 0x0F : packed >> 4);
                        red = green = blue = intensity;
                    } else if (format == 1) {
                        red = green = blue = tpl[source + pixel];
                    } else if (format == 2) {
                        const u8 packed = tpl[source + pixel];
                        alpha = Expand4(packed >> 4);
                        red = green = blue = Expand4(packed & 0x0F);
                    } else if (format == 3) {
                        red = green = blue = tpl[source + pixel * 2];
                        alpha = tpl[source + pixel * 2 + 1];
                    } else if (format == 4) {
                        const u16 packed = ReadBe16(tpl, source + pixel * 2);
                        red = Expand5(static_cast<u8>((packed >> 11) & 0x1F));
                        green = Expand6(static_cast<u8>((packed >> 5) & 0x3F));
                        blue = Expand5(static_cast<u8>(packed & 0x1F));
                    } else if (format == 5) {
                        const u16 packed = ReadBe16(tpl, source + pixel * 2);
                        if ((packed & 0x8000) != 0) {
                            red = Expand5(static_cast<u8>((packed >> 10) & 0x1F));
                            green = Expand5(static_cast<u8>((packed >> 5) & 0x1F));
                            blue = Expand5(static_cast<u8>(packed & 0x1F));
                        } else {
                            alpha = Expand3(static_cast<u8>((packed >> 12) & 0x07));
                            red = Expand4(static_cast<u8>((packed >> 8) & 0x0F));
                            green = Expand4(static_cast<u8>((packed >> 4) & 0x0F));
                            blue = Expand4(static_cast<u8>(packed & 0x0F));
                        }
                    } else {
                        alpha = tpl[source + pixel * 2];
                        red = tpl[source + pixel * 2 + 1];
                        green = tpl[source + 32 + pixel * 2];
                        blue = tpl[source + 32 + pixel * 2 + 1];
                    }
                    const u32 output_x = tile_x + x;
                    const u32 output_y = tile_y + y;
                    if (output_x < width && output_y < height) {
                        const std::size_t destination =
                            (static_cast<std::size_t>(output_y) * width + output_x) * 4;
                        rgba[destination] = red;
                        rgba[destination + 1] = green;
                        rgba[destination + 2] = blue;
                        rgba[destination + 3] = alpha;
                    }
                }
            }
            source += tile_bytes;
        }
    }
    TextureInfo descriptor{};
    descriptor.width = width;
    descriptor.height = height;
    return EncodePng(descriptor, rgba);
}

std::shared_ptr<std::vector<u8>> LoadSshdArchive(const FileSys::VirtualDir& root,
                                                 std::string_view path) {
    const auto cached = sshd_archive_cache.find(std::string(path));
    if (cached != sshd_archive_cache.end()) {
        return cached->second;
    }
    const auto source = ReadRomfsFile(root, path, MaxArchiveSize);
    if (!source) {
        return {};
    }
    auto archive = std::make_shared<std::vector<u8>>(std::move(*source));
    sshd_archive_cache.emplace(std::string(path), archive);
    return archive;
}

std::vector<u8> LoadSshdAsset(const FileSys::VirtualDir& root,
                              std::string_view logical_path) {
    struct Mapping {
        std::string_view logical;
        std::string_view archive;
        std::string_view member;
        bool texture;
    };
    static constexpr std::array<Mapping, 179> Assets{{
        {"sshd/raw/navi_table.dat", "/Object/NX/System.arc", "dat/navi_table.dat", false},
        {"sshd/raw/items.msbt", "/US/Object/en_US/0-Common.arc",
         "0-Common/003-ItemGet.msbt", false},
        {"sshd/raw/system.msbt", "/US/Object/en_US/0-Common.arc",
         "0-Common/002-System.msbt", false},
        {"sshd/raw/fi.msbt", "/US/Object/en_US/0-Common.arc",
         "0-Common/006-KenseiNormal.msbt", false},
        {"sshd/raw/fi_objectives.msbt", "/US/Object/en_US/0-Common.arc",
         "0-Common/006-2KenseiNormal.msbt", false},
        {"sshd/raw/map_text.msbt", "/US/Object/en_US/0-Common.arc",
         "0-Common/007-MapText.msbt", false},
        {"sshd/ui/parchment.png", "/Layout/MenuPause.arc", "timg/tr_kami_05.tpl", true},
        {"sshd/ui/parchment_dark.png", "/Layout/MenuPause.arc", "timg/tr_kami_08.tpl", true},
        {"sshd/ui/item_cell.png", "/Layout/MenuPause.arc", "timg/tr_itemBace_03.tpl", true},
        {"sshd/ui/item_cell_selected.png", "/Layout/MenuPause.arc", "timg/tr_itemBace_09.tpl", true},
        {"sshd/ui/cursor.png", "/Layout/MenuPause.arc", "timg/tr_defCursor_00.tpl", true},
        {"sshd/ui/heart.png", "/Layout/MenuPause.arc", "timg/tr_heartUtsuwa_01.tpl", true},
        {"sshd/ui/fi.png", "/Layout/MenuPause.arc", "timg/tr_phiIcon_00.tpl", true},
        {"sshd/ui/rupee_purse.png", "/Layout/MenuPause.arc", "timg/uk_purseA_00.tpl", true},
        {"sshd/ui/link_portrait.png", "/Layout/MenuPause.arc",
         "timg/tr_sabLinkCollect_04.tpl", true},
        {"sshd/item/practice_sword.png", "/Layout/MenuPause.arc", "timg/hm_swordA_00.tpl", true},
        {"sshd/item/goddess_white_sword.png", "/Layout/MenuPause.arc",
         "timg/hm_swordD_00.tpl", true},
        {"sshd/item/goddess_sword.png", "/Layout/MenuPause.arc", "timg/hm_swordB_00.tpl", true},
        {"sshd/item/goddess_longsword.png", "/Layout/MenuPause.arc",
         "timg/hm_swordC_00.tpl", true},
        {"sshd/item/master_sword.png", "/Layout/MenuPause.arc", "timg/hm_swordE_00.tpl", true},
        {"sshd/item/true_master_sword.png", "/Layout/MenuPause.arc",
         "timg/hm_swordMaster_00.tpl", true},
        {"sshd/item/sailcloth.png", "/Layout/MenuPause.arc", "timg/tr_stoleIcon_01.tpl", true},
        {"sshd/item/harp.png", "/Layout/MenuPause.arc", "timg/tr_tategoto_00.tpl", true},
        {"sshd/item/digging_mitts.png", "/Layout/MenuPause.arc",
         "timg/hm_moleGloveA_00.tpl", true},
        {"sshd/item/water_dragon_scale.png", "/Layout/MenuPause.arc",
         "timg/tr_urokoIcon_00.tpl", true},
        {"sshd/item/heart_piece.png", "/Layout/MenuPause.arc",
         "timg/tr_heartKake_02.tpl", true},
        {"sshd/item/heart_container.png", "/Layout/MenuPause.arc",
         "timg/tr_heartUtsuwa_01.tpl", true},
        {"sshd/item/triforce.png", "/Layout/MenuPause.arc", "timg/tr_tryForce_01.tpl", true},
        {"sshd/item/mogma_mitts.png", "/Layout/MenuPause.arc",
         "timg/hm_moleGloveB_00.tpl", true},
        {"sshd/item/wallet_a.png", "/Layout/MenuPause.arc", "timg/uk_purseA_00.tpl", true},
        {"sshd/item/wallet_b.png", "/Layout/MenuPause.arc", "timg/uk_purseB_00.tpl", true},
        {"sshd/item/wallet_c.png", "/Layout/MenuPause.arc", "timg/uk_purseC_00.tpl", true},
        {"sshd/item/wallet_d.png", "/Layout/MenuPause.arc", "timg/uk_purseD_00.tpl", true},
        {"sshd/item/wallet_e.png", "/Layout/MenuPause.arc", "timg/uk_purseE_00.tpl", true},
        {"sshd/item/fireshield_earrings.png", "/Layout/MenuPause.arc",
         "timg/hm_earring_00.tpl", true},
        {"sshd/item/cawlin_letter.png", "/Layout/MenuPause.arc",
         "timg/uk_kobunALetter_00.tpl", true},
        {"sshd/item/insect_cage.png", "/Layout/MenuPause.arc",
         "timg/uk_terryCage_00.tpl", true},
        {"sshd/item/rattle.png", "/Layout/MenuPause.arc", "timg/uk_garagara_00.tpl", true},
        {"sshd/item/emerald_tablet.png", "/Layout/MenuPause.arc",
         "timg/tr_sekiban_00.tpl", true},
        {"sshd/item/ruby_tablet.png", "/Layout/MenuPause.arc",
         "timg/tr_sekiban_01.tpl", true},
        {"sshd/item/amber_tablet.png", "/Layout/MenuPause.arc",
         "timg/tr_sekiban_02.tpl", true},
        {"sshd/item/life_tree_seedling.png", "/Layout/MenuPause.arc",
         "timg/uk_lifeNae_00.tpl", true},
        {"sshd/item/life_tree_fruit.png", "/Layout/MenuPause.arc",
         "timg/uk_lifeMi_00.tpl", true},
        {"sshd/item/stone_of_trials.png", "/Layout/MenuPause.arc",
         "timg/tr_sekibanStone_00.tpl", true},
        {"sshd/item/ballad_goddess.png", "/Layout/MenuPause.arc",
         "timg/tr_markMegami_00.tpl", true},
        {"sshd/item/farores_courage.png", "/Layout/MenuPause.arc",
         "timg/tr_markFlr_00.tpl", true},
        {"sshd/item/nayrus_wisdom.png", "/Layout/MenuPause.arc",
         "timg/tr_markNail_00.tpl", true},
        {"sshd/item/dins_power.png", "/Layout/MenuPause.arc", "timg/tr_markDin_00.tpl", true},
        {"sshd/item/faron_song_hero.png", "/Layout/MenuPause.arc",
         "timg/tr_markFlr_00.tpl", true},
        {"sshd/item/eldin_song_hero.png", "/Layout/MenuPause.arc",
         "timg/tr_markDin_00.tpl", true},
        {"sshd/item/lanayru_song_hero.png", "/Layout/MenuPause.arc",
         "timg/tr_markNail_00.tpl", true},
        {"sshd/item/song_of_hero.png", "/Layout/MenuPause.arc",
         "timg/tr_markMegami_00.tpl", true},
        // The B-wheel, pouch, bug and treasure icons below use the same
        // DoButton.arc members selected by dLytCommonIconItem.  In particular,
        // bug textures are not stored in ITEMFLAGS order.
        {"sshd/item/bow.png", "/Layout/DoButton.arc", "timg/uc_bowA_00.tpl", true},
        {"sshd/item/iron_bow.png", "/Layout/DoButton.arc", "timg/uc_bowB_00.tpl", true},
        {"sshd/item/sacred_bow.png", "/Layout/DoButton.arc", "timg/uc_bowC_00.tpl", true},
        {"sshd/item/bomb_bag.png", "/Layout/DoButton.arc", "timg/uc_bomb_00.tpl", true},
        {"sshd/item/beetle.png", "/Layout/DoButton.arc", "timg/uc_beetleA_00.tpl", true},
        {"sshd/item/hook_beetle.png", "/Layout/DoButton.arc", "timg/uc_beetleB_00.tpl", true},
        {"sshd/item/quick_beetle.png", "/Layout/DoButton.arc", "timg/uc_beetleC_00.tpl", true},
        {"sshd/item/tough_beetle.png", "/Layout/DoButton.arc", "timg/uc_beetleD_00.tpl", true},
        {"sshd/item/slingshot.png", "/Layout/DoButton.arc", "timg/uc_pachinkoA_00.tpl", true},
        {"sshd/item/scattershot.png", "/Layout/DoButton.arc", "timg/uc_pachinkoB_00.tpl", true},
        {"sshd/item/clawshots.png", "/Layout/DoButton.arc", "timg/uc_hookShot_00.tpl", true},
        {"sshd/item/bug_net.png", "/Layout/DoButton.arc", "timg/uc_netA_00.tpl", true},
        {"sshd/item/big_bug_net.png", "/Layout/DoButton.arc", "timg/uc_netB_00.tpl", true},
        {"sshd/item/whip.png", "/Layout/DoButton.arc", "timg/tr_whip_00.tpl", true},
        {"sshd/item/gust_bellows.png", "/Layout/DoButton.arc",
         "timg/tr_vacuumIcon_00.tpl", true},
        {"sshd/item/wooden_shield.png", "/Layout/DoButton.arc",
         "timg/uc_shieldWoodA_00.tpl", true},
        {"sshd/item/banded_shield.png", "/Layout/DoButton.arc",
         "timg/uc_shieldWoodB_00.tpl", true},
        {"sshd/item/braced_shield.png", "/Layout/DoButton.arc",
         "timg/uc_shieldWoodC_00.tpl", true},
        {"sshd/item/iron_shield.png", "/Layout/DoButton.arc",
         "timg/uc_shieldIronA_00.tpl", true},
        {"sshd/item/reinforced_shield.png", "/Layout/DoButton.arc",
         "timg/uc_shieldIronB_00.tpl", true},
        {"sshd/item/fortified_shield.png", "/Layout/DoButton.arc",
         "timg/uc_shieldIronC_00.tpl", true},
        {"sshd/item/sacred_shield.png", "/Layout/DoButton.arc",
         "timg/uc_shieldHolyA_00.tpl", true},
        {"sshd/item/divine_shield.png", "/Layout/DoButton.arc",
         "timg/uc_shieldHolyB_00.tpl", true},
        {"sshd/item/goddess_shield.png", "/Layout/DoButton.arc",
         "timg/uc_shieldHolyC_00.tpl", true},
        {"sshd/item/hylian_shield.png", "/Layout/DoButton.arc",
         "timg/uc_shieldHylia_00.tpl", true},
        {"sshd/item/small_seed_satchel.png", "/Layout/DoButton.arc",
         "timg/uc_spareSeedA_00.tpl", true},
        {"sshd/item/medium_seed_satchel.png", "/Layout/DoButton.arc",
         "timg/uc_spareSeedB_00.tpl", true},
        {"sshd/item/large_seed_satchel.png", "/Layout/DoButton.arc",
         "timg/uc_spareSeedC_00.tpl", true},
        {"sshd/item/small_quiver.png", "/Layout/DoButton.arc",
         "timg/uc_spareQuiverA_00.tpl", true},
        {"sshd/item/medium_quiver.png", "/Layout/DoButton.arc",
         "timg/uc_spareQuiverB_00.tpl", true},
        {"sshd/item/large_quiver.png", "/Layout/DoButton.arc",
         "timg/uc_spareQuiverC_00.tpl", true},
        {"sshd/item/small_bomb_bag.png", "/Layout/DoButton.arc",
         "timg/uc_spareBombBagA_00.tpl", true},
        {"sshd/item/medium_bomb_bag.png", "/Layout/DoButton.arc",
         "timg/uc_spareBombBagB_00.tpl", true},
        {"sshd/item/large_bomb_bag.png", "/Layout/DoButton.arc",
         "timg/uc_spareBombBagC_00.tpl", true},
        {"sshd/item/glittering_spores.png", "/Layout/DoButton.arc",
         "timg/uc_bottleKinokoB_00.tpl", true},
        {"sshd/item/water.png", "/Layout/DoButton.arc", "timg/uc_bottleWater_00.tpl", true},
        {"sshd/item/mushroom_spores.png", "/Layout/DoButton.arc",
         "timg/uc_bottleKinokoA_00.tpl", true},
        {"sshd/item/guardian_potion.png", "/Layout/DoButton.arc",
         "timg/uc_bottleMutekiA_00.tpl", true},
        {"sshd/item/guardian_potion_plus.png", "/Layout/DoButton.arc",
         "timg/uc_bottleMutekiB_00.tpl", true},
        {"sshd/item/sacred_water.png", "/Layout/DoButton.arc",
         "timg/uc_bottleHoly_00.tpl", true},
        {"sshd/item/heart_potion.png", "/Layout/DoButton.arc",
         "timg/uc_bottleKusuriA_00.tpl", true},
        {"sshd/item/heart_potion_plus.png", "/Layout/DoButton.arc",
         "timg/uc_bottleKusuriB_00.tpl", true},
        {"sshd/item/heart_potion_plus_plus.png", "/Layout/DoButton.arc",
         "timg/uc_bottleKusuriC_00.tpl", true},
        {"sshd/item/heart_potion_plus_plus_half.png", "/Layout/DoButton.arc",
         "timg/uc_bottleKusuriC_01.tpl", true},
        {"sshd/item/stamina_potion.png", "/Layout/DoButton.arc",
         "timg/uc_bottleGutsA_00.tpl", true},
        {"sshd/item/stamina_potion_plus.png", "/Layout/DoButton.arc",
         "timg/uc_bottleGutsB_00.tpl", true},
        {"sshd/item/air_potion.png", "/Layout/DoButton.arc",
         "timg/uc_bottleAirA_00.tpl", true},
        {"sshd/item/air_potion_plus.png", "/Layout/DoButton.arc",
         "timg/uc_bottleAirB_00.tpl", true},
        {"sshd/item/fairy_bottle.png", "/Layout/DoButton.arc",
         "timg/uc_bottleFairy_00.tpl", true},
        {"sshd/item/empty_bottle.png", "/Layout/DoButton.arc",
         "timg/uc_bottleEmpty_00.tpl", true},
        {"sshd/item/revitalizing_potion.png", "/Layout/DoButton.arc",
         "timg/uc_bottleRepairA_00.tpl", true},
        {"sshd/item/revitalizing_potion_plus.png", "/Layout/DoButton.arc",
         "timg/uc_bottleRepairB_00.tpl", true},
        {"sshd/item/revitalizing_potion_plus_plus.png", "/Layout/DoButton.arc",
         "timg/uc_bottleRepairC_01.tpl", true},
        {"sshd/item/revitalizing_potion_plus_plus_half.png", "/Layout/DoButton.arc",
         "timg/uc_bottleRepairC_00.tpl", true},
        {"sshd/item/hot_pumpkin_soup.png", "/Layout/DoButton.arc",
         "timg/uc_bottlePumpkinA_00.tpl", true},
        {"sshd/item/cold_pumpkin_soup.png", "/Layout/DoButton.arc",
         "timg/uc_bottlePumpkinB_00.tpl", true},
        {"sshd/item/heart_medal.png", "/Layout/DoButton.arc", "timg/uc_medalHeart_00.tpl", true},
        {"sshd/item/rupee_medal.png", "/Layout/DoButton.arc", "timg/uc_medalLucky_00.tpl", true},
        {"sshd/item/treasure_medal.png", "/Layout/DoButton.arc",
         "timg/uc_medalReturn_00.tpl", true},
        {"sshd/item/potion_medal.png", "/Layout/DoButton.arc",
         "timg/uc_medalDurable_00.tpl", true},
        {"sshd/item/cursed_medal.png", "/Layout/DoButton.arc", "timg/uc_medalDemon_00.tpl", true},
        {"sshd/item/life_medal.png", "/Layout/DoButton.arc", "timg/uc_medalLife_00.tpl", true},
        {"sshd/item/bug_medal.png", "/Layout/DoButton.arc", "timg/uc_medalHunter_00.tpl", true},
        {"sshd/item/faron_grasshopper.png", "/Layout/DoButton.arc",
         "timg/tr_bugIcon_08.tpl", true},
        {"sshd/item/woodland_rhino_beetle.png", "/Layout/DoButton.arc",
         "timg/tr_bugIcon_04.tpl", true},
        {"sshd/item/deku_hornet.png", "/Layout/DoButton.arc", "timg/tr_bugIcon_00.tpl", true},
        {"sshd/item/skyloft_mantis.png", "/Layout/DoButton.arc", "timg/tr_bugIcon_09.tpl", true},
        {"sshd/item/volcanic_ladybug.png", "/Layout/DoButton.arc",
         "timg/tr_bugIcon_05.tpl", true},
        {"sshd/item/blessed_butterfly.png", "/Layout/DoButton.arc",
         "timg/tr_bugIcon_01.tpl", true},
        {"sshd/item/lanayru_ant.png", "/Layout/DoButton.arc", "timg/tr_bugIcon_10.tpl", true},
        {"sshd/item/sand_cicada.png", "/Layout/DoButton.arc", "timg/tr_bugIcon_06.tpl", true},
        {"sshd/item/gerudo_dragonfly.png", "/Layout/DoButton.arc",
         "timg/tr_bugIcon_02.tpl", true},
        {"sshd/item/eldin_roller.png", "/Layout/DoButton.arc", "timg/tr_bugIcon_11.tpl", true},
        {"sshd/item/sky_stag_beetle.png", "/Layout/DoButton.arc",
         "timg/tr_bugIcon_07.tpl", true},
        {"sshd/item/starry_firefly.png", "/Layout/DoButton.arc",
         "timg/tr_bugIcon_03.tpl", true},
        {"sshd/item/hornet_larvae.png", "/Layout/DoButton.arc",
         "timg/uk_soaHachisu_00.tpl", true},
        {"sshd/item/bird_feather.png", "/Layout/DoButton.arc", "timg/uk_sobHaneA_00.tpl", true},
        {"sshd/item/tumbleweed.png", "/Layout/DoButton.arc", "timg/uk_socTumble_00.tpl", true},
        {"sshd/item/lizard_tail.png", "/Layout/DoButton.arc", "timg/tr_sozaiIcon_03.tpl", true},
        {"sshd/item/eldin_ore.png", "/Layout/DoButton.arc", "timg/uk_soeKoseki_00.tpl", true},
        {"sshd/item/ancient_flower.png", "/Layout/DoButton.arc", "timg/uk_sofHana_00.tpl", true},
        {"sshd/item/amber_relic.png", "/Layout/DoButton.arc",
         "timg/uk_sogMagatamaA_00.tpl", true},
        {"sshd/item/dusk_relic.png", "/Layout/DoButton.arc",
         "timg/uk_sohMagatamaB_00.tpl", true},
        {"sshd/item/jelly_blob.png", "/Layout/DoButton.arc", "timg/uk_soiPuyo_00.tpl", true},
        {"sshd/item/monster_claw.png", "/Layout/DoButton.arc", "timg/uk_sojClaw_00.tpl", true},
        {"sshd/item/monster_horn.png", "/Layout/DoButton.arc", "timg/uk_solHorn_00.tpl", true},
        {"sshd/item/ornamental_skull.png", "/Layout/DoButton.arc",
         "timg/uk_sokDokuroA_00.tpl", true},
        {"sshd/item/evil_crystal.png", "/Layout/DoButton.arc", "timg/uk_somEvil_00.tpl", true},
        {"sshd/item/blue_bird_feather.png", "/Layout/DoButton.arc",
         "timg/uk_sonHaneB_00.tpl", true},
        {"sshd/item/golden_skull.png", "/Layout/DoButton.arc",
         "timg/uk_sooDokuroB_00.tpl", true},
        {"sshd/item/goddess_plume.png", "/Layout/DoButton.arc",
         "timg/uk_sopGoddess_00.tpl", true},
        {"sshd/item/small_key.png", "/Layout/DoButton.arc", "timg/tr_keySmall_00.tpl", true},
        {"sshd/item/dungeon_map.png", "/Layout/DoButton.arc", "timg/tr_map_00.tpl", true},
        {"sshd/item/ancient_cistern_boss_key.png", "/Layout/DoButton.arc",
         "timg/hm_keyBoss2A_00.tpl", true},
        {"sshd/item/fire_sanctuary_boss_key.png", "/Layout/DoButton.arc",
         "timg/hm_keyBoss2B_00.tpl", true},
        {"sshd/item/sandship_boss_key.png", "/Layout/DoButton.arc",
         "timg/hm_keyBoss2C_00.tpl", true},
        {"sshd/item/skyview_boss_key.png", "/Layout/DoButton.arc",
         "timg/hm_keyBoss1A_00.tpl", true},
        {"sshd/item/earth_temple_boss_key.png", "/Layout/DoButton.arc",
         "timg/hm_keyBoss1B_00.tpl", true},
        {"sshd/item/lanayru_mining_facility_boss_key.png", "/Layout/DoButton.arc",
         "timg/hm_keyBoss1C_00.tpl", true},
        {"sshd/item/key_piece.png", "/Layout/DoButton.arc", "timg/ts_keyKakera_00.tpl", true},
        {"sshd/item/gratitude_crystal.png", "/Layout/DoButton.arc",
         "timg/uk_dawGenki_00.tpl", true},
        {"sshd/dowsing/target_00.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_00.tpl", true},
        {"sshd/dowsing/target_01.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_01.tpl", true},
        {"sshd/dowsing/target_02.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_02.tpl", true},
        {"sshd/dowsing/target_03.png", "/Layout/DoButton.arc", "timg/uk_dawGenki_00.tpl", true},
        {"sshd/dowsing/target_04.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_04.tpl", true},
        {"sshd/dowsing/target_05.png", "/Layout/DoButton.arc", "timg/uc_bottleHoly_00.tpl", true},
        {"sshd/dowsing/target_06.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_06.tpl", true},
        {"sshd/dowsing/target_07.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_07.tpl", true},
        {"sshd/dowsing/target_08.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_08.tpl", true},
        {"sshd/dowsing/target_09.png", "/Layout/DoButton.arc", "timg/tr_cameraMark_00.tpl", true},
        {"sshd/dowsing/target_10.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_10.tpl", true},
        {"sshd/dowsing/target_11.png", "/Layout/DoButton.arc", "timg/ts_keyKakera_00.tpl", true},
        {"sshd/dowsing/target_12.png", "/Layout/DoButton.arc", "timg/ts_dauzPump_00.tpl", true},
        {"sshd/dowsing/target_13.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_13.tpl", true},
        {"sshd/dowsing/target_14.png", "/Layout/DoButton.arc", "timg/ts_waterPot_00.tpl", true},
        {"sshd/dowsing/target_15.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_15.tpl", true},
        {"sshd/dowsing/target_16.png", "/Layout/DoButton.arc", "timg/ts_GdsCube_00.tpl", true},
        {"sshd/dowsing/target_17.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_17.tpl", true},
        {"sshd/dowsing/target_18.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_18.tpl", true},
        {"sshd/dowsing/target_19.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_19.tpl", true},
        {"sshd/dowsing/target_20.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_20.tpl", true},
        {"sshd/dowsing/target_21.png", "/Layout/DoButton.arc", "timg/tr_dauzTarget_21.tpl", true},
        {"sshd/map/world.png", "/Layout/Map2D.arc", "timg/tr_worldMapBace_00.tpl", true},
        {"sshd/map/faron.png", "/Layout/Map2D.arc", "timg/tr_worldMapMori_00.tpl", true},
        {"sshd/map/eldin.png", "/Layout/Map2D.arc", "timg/tr_worldMapKazan_00.tpl", true},
        {"sshd/map/lanayru.png", "/Layout/Map2D.arc", "timg/tr_worldMapSabaku_00.tpl", true},
    }};
    const auto cached = sshd_asset_cache.find(std::string(logical_path));
    if (cached != sshd_asset_cache.end()) {
        return cached->second;
    }
    const auto mapping = std::find_if(Assets.begin(), Assets.end(), [&](const Mapping& candidate) {
        return candidate.logical == logical_path;
    });
    if (mapping == Assets.end()) {
        return {};
    }
    const auto archive = LoadSshdArchive(root, mapping->archive);
    if (!archive) {
        return {};
    }
    const auto member = ExtractU8File(*archive, mapping->member);
    if (!member) {
        return {};
    }
    auto result = mapping->texture ? DecodeTplPng(*member) : std::move(*member);
    if (!result.empty()) {
        sshd_asset_cache.emplace(std::string(logical_path), result);
    }
    return result;
}









std::optional<std::vector<u8>> LoadNestedArchive(const FileSys::VirtualDir& root,
                                                 std::string_view outer_path,
                                                 std::string_view inner_name) {
    const auto outer_source = ReadRomfsFile(root, outer_path, MaxArchiveSize);
    if (!outer_source) {
        return std::nullopt;
    }
    const auto outer = DecompressYaz0(*outer_source);
    if (!outer) {
        return std::nullopt;
    }
    const auto inner_source = ExtractSarcFile(*outer, inner_name);
    if (!inner_source) {
        return std::nullopt;
    }
    return DecompressYaz0(*inner_source);
}

std::vector<u8> LoadBotwStock(const FileSys::VirtualDir& root, std::string_view actor) {
    const auto source = ReadRomfsFile(root, "/UI/StockItem/" + std::string(actor) + ".sbitemico",
                                      MaxSourceSize);
    if (!source) {
        return {};
    }
    const auto decoded = DecompressYaz0(*source);
    return decoded ? DecodeTexturePng(*decoded, actor, true) : std::vector<u8>{};
}

std::vector<u8> LoadBotwCommon(const FileSys::VirtualDir& root, std::string_view logical_path) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 54> Textures{{
        {"botw/ui/categories/Nt_PauseIconL_00_s.png", "Nt_PauseIconL_00^s"},
        {"botw/ui/categories/Nt_PauseIconL_01_s.png", "Nt_PauseIconL_01^s"},
        {"botw/ui/categories/Nt_PauseIconL_02_s.png", "Nt_PauseIconL_02^s"},
        {"botw/ui/categories/Nt_PauseIconL_03_s.png", "Nt_PauseIconL_03^s"},
        {"botw/ui/categories/Nt_PauseIconL_04_s.png", "Nt_PauseIconL_04^s"},
        {"botw/ui/categories/Nt_PauseIconL_05_s.png", "Nt_PauseIconL_05^s"},
        {"botw/ui/categories/Nt_PauseIconL_06_s.png", "Nt_PauseIconL_06^s"},
        {"botw/ui/attack.png", "Nt_PauseIconL_00^s"},
        {"botw/ui/defense.png", "Nt_PauseIconL_03^s"},
        {"botw/ui/bow.png", "Nt_PauseIconL_01^s"},
        {"botw/ui/guard.png", "Nt_PauseIconL_02^s"},
        {"botw/ui/heart.png", "Nt_HeartNew_00^f"},
        {"botw/ui/stamina.png", "Nt_GanbariLLnn_00^t"},
        {"botw/ui/rupee.png", "Nt_RupeeIconNew_00^l"},
        {"botw/ui/champion/grace.png", "Nt_IconSinJuNewMapL_00^s"},
        {"botw/ui/champion/fury.png", "Nt_IconSinJuNewMapL_01^s"},
        {"botw/ui/champion/gale.png", "Nt_IconSinJuNewMapL_02^s"},
        {"botw/ui/champion/protection.png", "Nt_IconSinJuNewMapL_03^s"},
        {"botw/ui/champion/grace_shadow.png", "Nt_IconSinJuNewMapShL_00^s"},
        {"botw/ui/champion/fury_shadow.png", "Nt_IconSinJuNewMapShL_01^s"},
        {"botw/ui/champion/gale_shadow.png", "Nt_IconSinJuNewMapShL_02^s"},
        {"botw/ui/champion/protection_shadow.png", "Nt_IconSinJuNewMapShL_03^s"},
        {"botw/ui/map/tower.png", "Nt_MapTowerNew_00^t"},
        {"botw/ui/map/tower_outline.png", "Nt_MapTowerNew_01^t"},
        {"botw/ui/map/tower_glow.png", "Nt_MapTowerNew_02^t"},
        {"botw/ui/map/tower_flash.png", "Nt_MapTowerNew_03^t"},
        {"botw/ui/map/stable.png", "Nt_MapHatago_00^t"},
        {"botw/ui/map/lab.png", "Nt_MapReserchCenter_00^t"},
        {"botw/ui/map/village.png", "Nt_MapCityIcon_00^t"},
        {"botw/ui/map/settlement.png", "Nt_MapCityIcon_00^t"},
        {"botw/ui/map/shrine.png", "Nt_MapDungeonNew_00^t"},
        {"botw/ui/map/shrine_outline.png", "Nt_MapDungeonNew_01^t"},
        {"botw/ui/map/shrine_glow.png", "Nt_MapDungeonNew_02^t"},
        {"botw/ui/map/shrine_flash.png", "Nt_MapDungeonNew_03^t"},
        {"botw/ui/map/shrine_dlc.png", "Sd_MapDungeonNewDLC_00^t"},
        {"botw/ui/map/shrine_dlc_outline.png", "Sd_MapDungeonNewDLC_01^t"},
        {"botw/ui/map/shrine_dlc_glow.png", "Sd_MapDungeonNewDLC_02^t"},
        {"botw/ui/map/shrine_dlc_flash.png", "Sd_MapDungeonNewDLC_03^t"},
        {"botw/ui/map/fountain.png", "Nt_MapEtcIcon_00^t"},
        {"botw/ui/map/statue.png", "Nt_MapEtcIcon_00^t"},
        {"botw/ui/map/memory.png", "SSIconPictureEX_00^s"},
        {"botw/ui/map/castle.png", "Nt_MapCastle_00^t"},
        {"botw/ui/map/shrine_resurrection.png", "Nt_MapStartPointNew_00^t"},
        {"botw/ui/map/start.png", "Nt_MapStartPointNew_00^t"},
        {"botw/ui/map/korok.png", "Nt_MapIconKolog^t"},
        {"botw/ui/map/guardian.png", "Nt_MapIconGuardian_00^t"},
        {"botw/ui/map/challenge.png", "Nt_MapChallengeIcon_00^s"},
        {"botw/ui/map/player.png", "Nt_MapPlayerIconNew_00^s"},
        {"botw/ui/map/stamps.png", "Nt_MapStampIconMarge_00^f"},
        {"botw/ui/map/pin_red.png", "Nt_MapMapPinIcon1_00^s"},
        {"botw/ui/map/pin_blue.png", "Nt_MapMapPinIcon2_00^s"},
        {"botw/ui/map/pin_yellow.png", "Nt_MapMapPinIcon3_00^s"},
        {"botw/ui/map/pin_green.png", "Nt_MapMapPinIcon4_00^s"},
        {"botw/ui/map/pin_purple.png", "Nt_MapMapPinIcon5_00^s"},
    }};
    const auto cached = botw_common_cache.find(std::string(logical_path));
    if (cached != botw_common_cache.end()) {
        return cached->second;
    }
    const auto mapping = std::find_if(Textures.begin(), Textures.end(), [&](const auto& entry) {
        return entry.first == logical_path;
    });
    if (mapping == Textures.end()) {
        return {};
    }
    if (!botw_common_archive) {
        if (std::chrono::steady_clock::now() < botw_common_retry_after) {
            return {};
        }
        auto archive = LoadNestedArchive(root, "/Pack/Bootup.pack", "Layout/Common.sblarc");
        if (!archive) {
            botw_common_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
            return {};
        }
        botw_common_archive = std::make_shared<std::vector<u8>>(std::move(*archive));
        botw_common_loaded = true;
        botw_common_retry_after = {};
    }
    auto png = DecodeTexturePng(*botw_common_archive, mapping->second);
    if (!png.empty()) {
        botw_common_cache.emplace(std::string(logical_path), png);
    }
    return png;
}

std::vector<u8> LoadBotwMap(const FileSys::VirtualDir& root) {
    static constexpr std::array<char, 12> Columns{'Z', 'A', 'B', 'C', 'D', 'E',
                                                   'F', 'G', 'H', 'I', 'J', 'K'};
    constexpr u32 Rows = 10;
    std::vector<u8> canvas;
    u32 tile_width{};
    u32 tile_height{};
    for (u32 column = 0; column < Columns.size(); ++column) {
        for (u32 row = 0; row < Rows; ++row) {
            const std::string path = "/UI/MapTex/MainField/MapTex3_" +
                                     std::string(1, Columns[column]) + "-" +
                                     std::to_string(row) + ".sbmaptex";
            const auto source = ReadRomfsFile(root, path, MaxSourceSize);
            if (!source) {
                return {};
            }
            const auto decoded_source = DecompressYaz0(*source);
            if (!decoded_source) {
                return {};
            }
            const auto tile = DecodeTextureRgba(*decoded_source, {}, true);
            if (!tile) {
                return {};
            }
            if (canvas.empty()) {
                tile_width = tile->width;
                tile_height = tile->height;
                const u64 output_width = static_cast<u64>(tile_width) * Columns.size();
                const u64 output_height = static_cast<u64>(tile_height) * 12;
                if (tile_width == 0 || tile_height == 0 || output_width > MaxTextureDimension ||
                    output_height > MaxTextureDimension ||
                    output_width * output_height > MaxTexturePixels) {
                    return {};
                }
                canvas.resize(static_cast<std::size_t>(output_width * output_height * 4));
            } else if (tile->width != tile_width || tile->height != tile_height) {
                return {};
            }
            const u32 output_width = tile_width * Columns.size();
            const u32 output_row = row + 1;
            for (u32 y = 0; y < tile_height; ++y) {
                const std::size_t destination =
                    (static_cast<std::size_t>(output_row * tile_height + y) * output_width +
                     column * tile_width) * 4;
                const std::size_t source_offset = static_cast<std::size_t>(y) * tile_width * 4;
                std::memcpy(canvas.data() + destination, tile->rgba.data() + source_offset,
                            static_cast<std::size_t>(tile_width) * 4);
            }
        }
    }
    TextureInfo descriptor{};
    descriptor.width = tile_width * Columns.size();
    descriptor.height = tile_height * 12;
    return EncodePng(descriptor, canvas);
}

std::vector<u8> LoadMk8dCharacter(const FileSys::VirtualDir& root, std::string_view logical_path) {
    const auto name = LogicalComponent(logical_path, "mk8d/characters/", ".png");
    if (!name || !name->starts_with("tc_MapChara_")) {
        return {};
    }
    const auto cached = mk8d_character_cache.find(std::string(logical_path));
    if (cached != mk8d_character_cache.end()) {
        return cached->second;
    }
    if (!mk8d_character_archive) {
        if (mk8d_character_failed) {
            return {};
        }
        auto archive = LoadNestedArchive(root, "/UI/cmn/common.sarc", "cm_L_CharaIcon_00.szs");
        if (!archive) {
            mk8d_character_failed = true;
            return {};
        }
        mk8d_character_archive = std::make_shared<std::vector<u8>>(std::move(*archive));
    }
    auto png = DecodeTexturePng(*mk8d_character_archive, *name + "^l");
    if (!png.empty()) {
        mk8d_character_cache.emplace(std::string(logical_path), png);
    }
    return png;
}

std::vector<u8> LoadMk8dItem(const FileSys::VirtualDir& root, std::string_view logical_path) {
    static constexpr std::array<std::string_view, 23> Textures{
        "tc_Item_Banana^l",       "tc_Item_Koura^l",       "tc_Item_RedKoura^l",
        "tc_Item_Mush^l",         "tc_Item_Bomb^l",        "tc_Item_Gesso^l",
        "tc_Item_Togezo^l",       "tc_Item_Mush3^l",       "tc_Item_Star^l",
        "tc_Item_Killer^l",       "tc_Item_Thunder^l",     "tc_Item_PKinoko^l",
        "tc_Item_Flower^l",       "tc_Item_PackunFlower^l", "tc_Item_Boomerang^l",
        "tc_Item_Coin^l",         "tc_Item_SuperHorn^l",   "tc_Item_Banana3^l",
        "tc_Item_Koura3^l",       "tc_Item_RedKoura3^l",   "tc_Item_SP8^l",
        "tc_Item_Feather^l",      "tc_Item_Teresa^l",
    };
    const auto item = LogicalComponent(logical_path, "mk8d/items/item_", ".png");
    if (!item || !std::all_of(item->begin(), item->end(), [](char value) { return std::isdigit(value); })) {
        return {};
    }
    std::size_t index{};
    for (const char digit : *item) {
        if (index > 1000) {
            return {};
        }
        index = index * 10 + static_cast<std::size_t>(digit - '0');
    }
    if (index >= Textures.size()) {
        return {};
    }
    const auto cached = mk8d_item_cache.find(std::string(logical_path));
    if (cached != mk8d_item_cache.end()) {
        return cached->second;
    }
    if (!mk8d_item_archive) {
        if (mk8d_item_failed) {
            return {};
        }
        auto archive = LoadNestedArchive(root, "/UI/cmn/race.sarc", "rc_L_ItemBox_00.szs");
        if (!archive) {
            mk8d_item_failed = true;
            return {};
        }
        mk8d_item_archive = std::make_shared<std::vector<u8>>(std::move(*archive));
    }
    auto png = DecodeTexturePng(*mk8d_item_archive, Textures[index]);
    if (!png.empty()) {
        mk8d_item_cache.emplace(std::string(logical_path), png);
    }
    return png;
}

std::vector<u8> LoadMk8dCommon(const FileSys::VirtualDir& root, std::string_view logical_path) {
    if (logical_path != "mk8d/ui/lap_flag.png") {
        return {};
    }
    if (!mk8d_common_loaded) {
        mk8d_common_loaded = true;
        const auto archive = LoadNestedArchive(root, "/UI/cmn/race.sarc", "rc_L_Lap_00.szs");
        if (archive) {
            auto png = DecodeTexturePng(*archive, "ym_LapFlag_00^f");
            if (!png.empty()) {
                mk8d_common_cache.emplace("mk8d/ui/lap_flag.png", std::move(png));
            }
        }
    }
    const auto found = mk8d_common_cache.find(std::string(logical_path));
    return found != mk8d_common_cache.end() ? found->second : std::vector<u8>{};
}

std::vector<u8> LoadMk8dMap(const FileSys::VirtualDir& root, std::string_view logical_path) {
    const auto course = LogicalComponent(logical_path, "mk8d/maps/", ".png");
    if (!course) {
        return {};
    }
    const auto source = ReadRomfsFile(root, "/Course/" + *course + "/course_maptexture.bntx",
                                      MaxSourceSize);
    return source ? DecodeTexturePng(*source, {}, true) : std::vector<u8>{};
}

std::vector<u8> LoadMk8dMapCamera(const FileSys::VirtualDir& root,
                                  std::string_view logical_path) {
    const auto course = LogicalComponent(logical_path, "mk8d/maps/", ".mapcamera");
    if (!course) {
        return {};
    }
    const auto source = ReadRomfsFile(root, "/Course/" + *course + "/course_mapcamera.bin", 4096);
    return source.value_or(std::vector<u8>{});
}

constexpr u64 XxhPrime1 = 11400714785074694791ULL;
constexpr u64 XxhPrime2 = 14029467366897019727ULL;
constexpr u64 XxhPrime3 = 1609587929392839161ULL;
constexpr u64 XxhPrime4 = 9650029242287828579ULL;
constexpr u64 XxhPrime5 = 2870177450012600261ULL;

u64 RotateLeft(u64 value, u32 count) {
    return (value << count) | (value >> (64U - count));
}

u64 XxhRound(u64 accumulator, u64 lane) {
    accumulator += lane * XxhPrime2;
    accumulator = RotateLeft(accumulator, 31);
    return accumulator * XxhPrime1;
}

u64 XxhMergeRound(u64 accumulator, u64 lane) {
    accumulator ^= XxhRound(0, lane);
    return accumulator * XxhPrime1 + XxhPrime4;
}

u64 Xxh64(std::string_view source) {
    const auto* data = reinterpret_cast<const u8*>(source.data());
    const std::size_t length = source.size();
    std::size_t cursor{};
    u64 value{};
    if (length >= 32) {
        u64 v1 = XxhPrime1 + XxhPrime2;
        u64 v2 = XxhPrime2;
        u64 v3 = 0;
        u64 v4 = 0 - XxhPrime1;
        const std::size_t limit = length - 32;
        while (cursor <= limit) {
            v1 = XxhRound(v1, ReadU64(std::span<const u8>{data, length}, cursor));
            v2 = XxhRound(v2, ReadU64(std::span<const u8>{data, length}, cursor + 8));
            v3 = XxhRound(v3, ReadU64(std::span<const u8>{data, length}, cursor + 16));
            v4 = XxhRound(v4, ReadU64(std::span<const u8>{data, length}, cursor + 24));
            cursor += 32;
        }
        value = RotateLeft(v1, 1) + RotateLeft(v2, 7) + RotateLeft(v3, 12) +
                RotateLeft(v4, 18);
        value = XxhMergeRound(value, v1);
        value = XxhMergeRound(value, v2);
        value = XxhMergeRound(value, v3);
        value = XxhMergeRound(value, v4);
    } else {
        value = XxhPrime5;
    }
    value += length;
    while (cursor + 8 <= length) {
        value ^= XxhRound(0, ReadU64(std::span<const u8>{data, length}, cursor));
        value = RotateLeft(value, 27) * XxhPrime1 + XxhPrime4;
        cursor += 8;
    }
    if (cursor + 4 <= length) {
        value ^= static_cast<u64>(ReadU32(std::span<const u8>{data, length}, cursor)) * XxhPrime1;
        value = RotateLeft(value, 23) * XxhPrime2 + XxhPrime3;
        cursor += 4;
    }
    while (cursor < length) {
        value ^= static_cast<u64>(data[cursor++]) * XxhPrime5;
        value = RotateLeft(value, 11) * XxhPrime1;
    }
    value ^= value >> 33;
    value *= XxhPrime2;
    value ^= value >> 29;
    value *= XxhPrime3;
    value ^= value >> 32;
    return value;
}

std::string NormalizeXcxArchivePath(std::string_view source) {
    std::string result;
    result.reserve(source.size() + 1);
    if (source.empty() || source.front() != '/') {
        result.push_back('/');
    }
    for (const char character : source) {
        const char normalized = character == '\\' ? '/' : character;
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(normalized))));
    }
    return result;
}

bool LoadXcxArchiveIndex(const FileSys::VirtualDir& root) {
    if (xcx_archive_loaded) {
        return xcx_archive_store != nullptr;
    }
    if (std::chrono::steady_clock::now() < xcx_archive_retry_after) {
        return false;
    }
    const auto index = ReadRomfsFile(root, "/sts.arh", 4 * 1024 * 1024);
    const auto store = root ? root->GetFileRelative("/sts.ard") : nullptr;
    if (!index || index->size() < 16 || !HasMagic(*index, "arh2") || !store) {
        xcx_archive_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const u32 count = ReadU32(*index, 4);
    const u32 alignment = ReadU32(*index, 8);
    if (count == 0 || count > 500'000 || alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        static_cast<u64>(count) * 16 + 16 > index->size()) {
        xcx_archive_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }

    std::unordered_map<u64, XcxArchiveRange> ranges;
    ranges.reserve(count);
    u64 offset{};
    const u64 store_size = store->GetSize();
    for (u32 member = 0; member < count; ++member) {
        const std::size_t entry = 16 + static_cast<std::size_t>(member) * 16;
        const u64 path_hash = ReadU64(*index, entry);
        const u32 disk_size = ReadU32(*index, entry + 8);
        const u32 expanded_size = ReadU32(*index, entry + 12);
        if (disk_size == 0 || expanded_size == 0 || offset > store_size ||
            disk_size > store_size - offset ||
            offset > std::numeric_limits<u64>::max() - disk_size - (alignment - 1)) {
            xcx_archive_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
            return false;
        }
        ranges.emplace(path_hash, XcxArchiveRange{offset, disk_size, expanded_size});
        offset = (offset + disk_size + alignment - 1) & ~(static_cast<u64>(alignment) - 1);
    }
    xcx_archive_index = std::move(ranges);
    xcx_archive_store = store;
    xcx_archive_loaded = true;
    xcx_archive_retry_after = {};
    return true;
}

std::optional<std::vector<u8>> ExpandXcxArchiveMember(std::span<const u8> member,
                                                       u32 expected_size) {
    if (!HasMagic(member, "xbc1")) {
        if (member.size() != expected_size || member.size() > MaxDecodedSourceSize) {
            return std::nullopt;
        }
        return std::vector<u8>{member.begin(), member.end()};
    }
    if (member.size() < 0x30) {
        return std::nullopt;
    }
    const u32 compression = ReadU32(member, 4);
    const u32 expanded_size = ReadU32(member, 8);
    const u32 compressed_size = ReadU32(member, 12);
    if (expanded_size != expected_size || expanded_size == 0 ||
        expanded_size > MaxDecodedSourceSize || compressed_size == 0 ||
        compressed_size > member.size() - 0x30) {
        return std::nullopt;
    }
    const auto payload = member.subspan(0x30, compressed_size);
    std::vector<u8> result;
    if (compression == 0) {
        if (payload.size() != expanded_size) {
            return std::nullopt;
        }
        result.assign(payload.begin(), payload.end());
    } else if (compression == 1) {
        result.resize(expanded_size);
        uLongf destination_size = expanded_size;
        if (uncompress(result.data(), &destination_size, payload.data(), payload.size()) != Z_OK ||
            destination_size != expanded_size) {
            return std::nullopt;
        }
    } else if (compression == 3) {
        result = Common::Compression::DecompressDataZSTD(payload);
    } else {
        return std::nullopt;
    }
    return result.size() == expanded_size ? std::optional{std::move(result)} : std::nullopt;
}

std::optional<std::vector<u8>> ReadXcxArchiveMember(const FileSys::VirtualDir& root,
                                                     std::string_view path) {
    if (!LoadXcxArchiveIndex(root) || !xcx_archive_store) {
        return std::nullopt;
    }
    const std::string normalized = NormalizeXcxArchivePath(path);
    const auto found = xcx_archive_index.find(Xxh64(normalized));
    if (found == xcx_archive_index.end() || found->second.disk_size > MaxDecodedSourceSize) {
        return std::nullopt;
    }
    auto member = xcx_archive_store->ReadBytes(found->second.disk_size, found->second.offset);
    if (member.size() != found->second.disk_size) {
        return std::nullopt;
    }
    return ExpandXcxArchiveMember(member, found->second.expanded_size);
}

u32 XcxMurmur3(std::string_view text) {
    constexpr u32 C1 = 0xCC9E2D51;
    constexpr u32 C2 = 0x1B873593;
    u32 hash{};
    std::size_t cursor{};
    while (cursor + 4 <= text.size()) {
        u32 value = static_cast<u8>(text[cursor]) |
                    (static_cast<u32>(static_cast<u8>(text[cursor + 1])) << 8) |
                    (static_cast<u32>(static_cast<u8>(text[cursor + 2])) << 16) |
                    (static_cast<u32>(static_cast<u8>(text[cursor + 3])) << 24);
        value *= C1;
        value = std::rotl(value, 15);
        value *= C2;
        hash ^= value;
        hash = std::rotl(hash, 13) * 5 + 0xE6546B64;
        cursor += 4;
    }
    u32 tail{};
    switch (text.size() - cursor) {
    case 3:
        tail ^= static_cast<u32>(static_cast<u8>(text[cursor + 2])) << 16;
        [[fallthrough]];
    case 2:
        tail ^= static_cast<u32>(static_cast<u8>(text[cursor + 1])) << 8;
        [[fallthrough]];
    case 1:
        tail ^= static_cast<u8>(text[cursor]);
        tail *= C1;
        tail = std::rotl(tail, 15);
        tail *= C2;
        hash ^= tail;
        break;
    default:
        break;
    }
    hash ^= static_cast<u32>(text.size());
    hash ^= hash >> 16;
    hash *= 0x85EBCA6B;
    hash ^= hash >> 13;
    hash *= 0xC2B2AE35;
    hash ^= hash >> 16;
    return hash;
}

std::size_t XcxBdatValueSize(u8 type) {
    switch (type) {
    case 1:
    case 4:
    case 10:
    case 12:
        return 1;
    case 2:
    case 5:
    case 13:
        return 2;
    case 3:
    case 6:
    case 7:
    case 8:
    case 9:
    case 11:
        return 4;
    default:
        return 0;
    }
}

struct XcxBdatColumn {
    u8 type{};
    u32 offset{};
};

struct XcxBdatTable {
    std::span<const u8> file;
    std::size_t base{};
    u32 rows{};
    u32 base_id{};
    u32 row_offset{};
    u32 row_length{};
    u32 string_offset{};
    u32 string_length{};
    std::unordered_map<u32, XcxBdatColumn> columns;

    std::optional<std::span<const u8>> RowByIndex(u32 index) const {
        const u64 offset = static_cast<u64>(base) + row_offset +
                           static_cast<u64>(index) * row_length;
        if (index >= rows || offset > file.size() || row_length > file.size() - offset) {
            return std::nullopt;
        }
        return file.subspan(static_cast<std::size_t>(offset), row_length);
    }

    std::optional<std::span<const u8>> RowById(u32 id) const {
        return id < base_id ? std::nullopt : RowByIndex(id - base_id);
    }

    const XcxBdatColumn* Column(std::string_view name) const {
        const auto found = columns.find(XcxMurmur3(name));
        return found == columns.end() ? nullptr : &found->second;
    }

    u32 Unsigned(std::span<const u8> row, std::string_view name, u32 fallback = 0) const {
        const auto* column = Column(name);
        if (!column || column->offset >= row.size()) {
            return fallback;
        }
        switch (column->type) {
        case 1:
        case 10:
        case 12:
            return row[column->offset];
        case 2:
        case 13:
            return ReadU16(row, column->offset);
        case 3:
        case 9:
            return ReadU32(row, column->offset);
        default:
            return fallback;
        }
    }

    s32 Signed(std::span<const u8> row, std::string_view name, s32 fallback = 0) const {
        const auto* column = Column(name);
        if (!column || column->offset >= row.size()) {
            return fallback;
        }
        switch (column->type) {
        case 4:
            return static_cast<s8>(row[column->offset]);
        case 5:
            return static_cast<s16>(ReadU16(row, column->offset));
        case 6:
            return static_cast<s32>(ReadU32(row, column->offset));
        default:
            return static_cast<s32>(Unsigned(row, name, static_cast<u32>(fallback)));
        }
    }

    float Floating(std::span<const u8> row, std::string_view name) const {
        const auto* column = Column(name);
        if (!column || column->type != 8 || column->offset > row.size() ||
            row.size() - column->offset < sizeof(float)) {
            return 0.0F;
        }
        float value{};
        std::memcpy(&value, row.data() + column->offset, sizeof(value));
        return std::isfinite(value) ? value : 0.0F;
    }

    std::string String(std::span<const u8> row, std::string_view name) const {
        const auto* column = Column(name);
        if (!column || (column->type != 7 && column->type != 11) ||
            column->offset > row.size() || row.size() - column->offset < 4) {
            return {};
        }
        const u32 relative = ReadU32(row, column->offset);
        const u64 begin = static_cast<u64>(base) + string_offset + relative;
        const u64 end = static_cast<u64>(base) + string_offset + string_length;
        if (begin >= end || end > file.size()) {
            return {};
        }
        const char* first = reinterpret_cast<const char*>(file.data() + begin);
        const char* last = reinterpret_cast<const char*>(file.data() + end);
        const char* terminator = std::find(first, last, '\0');
        return std::string(first, terminator);
    }
};

std::optional<XcxBdatTable> FindXcxBdatTable(std::span<const u8> file,
                                             std::string_view table_name) {
    if (file.size() < 16 || !HasMagic(file, "BDAT") ||
        ReadU32(file, 4) != 0x01001004) {
        return std::nullopt;
    }
    const u32 count = ReadU32(file, 8);
    if (count == 0 || count > 65'536 || static_cast<u64>(count) * 4 + 16 > file.size()) {
        return std::nullopt;
    }
    const u32 wanted = XcxMurmur3(table_name);
    for (u32 index = 0; index < count; ++index) {
        const u32 base = ReadU32(file, 16 + index * 4);
        if (base > file.size() || file.size() - base < 48 ||
            !HasMagic(file.subspan(base), "BDAT") || ReadU32(file, base + 4) != 0x3004) {
            continue;
        }
        const u32 columns = ReadU32(file, base + 8);
        const u32 rows = ReadU32(file, base + 12);
        const u32 base_id = ReadU32(file, base + 16);
        const u32 column_offset = ReadU32(file, base + 24);
        const u32 row_offset = ReadU32(file, base + 32);
        const u32 row_length = ReadU32(file, base + 36);
        const u32 string_offset = ReadU32(file, base + 40);
        const u32 string_length = ReadU32(file, base + 44);
        if (columns == 0 || columns > 4096 || rows > 1'000'000 || row_length == 0 ||
            static_cast<u64>(base) + column_offset + static_cast<u64>(columns) * 3 > file.size() ||
            static_cast<u64>(base) + row_offset + static_cast<u64>(rows) * row_length > file.size() ||
            static_cast<u64>(base) + string_offset + string_length > file.size() ||
            string_length < 5 || file[base + string_offset] != 0 ||
            ReadU32(file, base + string_offset + 1) != wanted) {
            continue;
        }
        XcxBdatTable table{file, base, rows, base_id, row_offset, row_length,
                           string_offset, string_length, {}};
        table.columns.reserve(columns);
        u32 value_offset{};
        for (u32 column_index = 0; column_index < columns; ++column_index) {
            const u32 definition = base + column_offset + column_index * 3;
            const u8 type = file[definition];
            const u16 name_offset = ReadU16(file, definition + 1);
            const std::size_t value_size = XcxBdatValueSize(type);
            const u64 name = static_cast<u64>(base) + string_offset + name_offset;
            if (value_size == 0 || value_offset + value_size > row_length ||
                name > file.size() || file.size() - name < 4) {
                return std::nullopt;
            }
            table.columns.emplace(ReadU32(file, static_cast<std::size_t>(name)),
                                  XcxBdatColumn{type, value_offset});
            value_offset += static_cast<u32>(value_size);
        }
        return table;
    }
    return std::nullopt;
}

void XcxAppendU16(std::vector<u8>& output, u16 value) {
    output.push_back(static_cast<u8>(value));
    output.push_back(static_cast<u8>(value >> 8));
}

void XcxAppendU32(std::vector<u8>& output, u32 value) {
    for (u32 shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<u8>(value >> shift));
    }
}

void XcxAppendFloat(std::vector<u8>& output, float value) {
    u32 bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    XcxAppendU32(output, bits);
}

std::shared_ptr<std::vector<u8>> LoadXcxBdat(const FileSys::VirtualDir& root,
                                             std::string_view path,
                                             std::shared_ptr<std::vector<u8>>& cache) {
    if (!cache) {
        auto data = ReadXcxArchiveMember(root, path);
        if (data) {
            cache = std::make_shared<std::vector<u8>>(std::move(*data));
        }
    }
    return cache;
}

std::optional<u32> XcxSegmentMapRow(u32 zone) {
    switch (zone) {
    case 1001:
    case 1101:
    case 1201:
    case 1301:
    case 1401:
        return 13;
    case 1501:
        return 6;
    case 1601:
        return 7;
    case 1701:
        return 8;
    case 2001:
        return 9;
    case 2201:
        return 10;
    case 5000:
        return 12;
    default:
        return std::nullopt;
    }
}

std::vector<u8> LoadXcxMapMetadata(const FileSys::VirtualDir& root, u32 zone) {
    const auto common = LoadXcxBdat(root, "/bdat/common.bdat", xcx_common_bdat);
    const auto messages = LoadXcxBdat(root, "/bdat/us/common_ms.bdat", xcx_message_bdat);
    const auto bounds_id = XcxSegmentMapRow(zone);
    if (!common || !messages || !bounds_id) {
        return {};
    }
    const auto bounds = FindXcxBdatTable(*common, "SegmentMapInfo");
    const auto markers = FindXcxBdatTable(*common, "MNU_SegMapInfo" + std::to_string(zone));
    const auto locations = FindXcxBdatTable(*common, "FLD_Location");
    const auto skip_travel = FindXcxBdatTable(*common, "FLD_SkipTravel");
    const auto formations = FindXcxBdatTable(*common, "FLD_FormationList");
    const auto terminals = FindXcxBdatTable(*common, "SEG_Terminal" + std::to_string(zone));
    const auto field_list = FindXcxBdatTable(*common, "FLD_FieldNameList");
    const auto field_names = FindXcxBdatTable(*messages, "fieldnamelist_ms");
    const auto bounds_row = bounds ? bounds->RowById(*bounds_id) : std::nullopt;
    if (!bounds || !markers || !locations || !skip_travel || !formations || !bounds_row) {
        return {};
    }

    struct MarkerRecord {
        u32 info_id{};
        u16 discovery_flag{};
        u16 open_flag{};
        u32 skip_travel_id{};
        u32 formation_id{};
        u16 probe_id{};
        u8 kind{};
        u8 icon{};
        u8 location_type{};
        u8 flags{};
        float world_x{};
        float world_z{};
        float offset_x{};
        float offset_y{};
        std::string name;
    };
    const auto read_location = [&](u32 id, MarkerRecord& record) {
        const auto location = locations->RowById(id);
        if (!location) {
            return;
        }
        record.discovery_flag =
            static_cast<u16>(locations->Unsigned(*location, "flg"));
        record.skip_travel_id = locations->Unsigned(*location, "SkipTravel_id");
        record.location_type =
            static_cast<u8>(locations->Unsigned(*location, "Loc_type"));
        const u32 name_id = locations->Unsigned(*location, "Loc_name");
        if (const auto name_row = field_names ? field_names->RowById(name_id) : std::nullopt) {
            record.name = field_names->String(*name_row, "name");
        }
        if (const auto skip = skip_travel->RowById(record.skip_travel_id)) {
            record.formation_id = skip_travel->Unsigned(*skip, "formationId");
            const u32 skip_flag = skip_travel->Unsigned(*skip, "loc_flg");
            if (skip_flag != 0) {
                record.discovery_flag = static_cast<u16>(skip_flag);
            }
            if (record.formation_id != 0) {
                record.flags |= 1U; // Native fast-travel destination.
            }
        }
    };

    std::vector<MarkerRecord> records;
    records.reserve(markers->rows + (terminals ? terminals->rows : 0));
    for (u32 index = 0; index < markers->rows; ++index) {
        const auto row = markers->RowByIndex(index);
        if (!row) {
            return {};
        }
        MarkerRecord record{};
        record.info_id = markers->Unsigned(*row, "infoID");
        record.kind = static_cast<u8>(markers->Unsigned(*row, "type"));
        record.icon = static_cast<u8>(markers->Unsigned(*row, "icon"));
        record.discovery_flag = static_cast<u16>(markers->Unsigned(*row, "flag"));
        record.world_x = markers->Floating(*row, "posX");
        record.world_z = markers->Floating(*row, "posZ");
        record.offset_x = markers->Floating(*row, "offsetX");
        record.offset_y = markers->Floating(*row, "offsetY");
        read_location(record.info_id, record);
        records.emplace_back(std::move(record));
    }

    if (terminals) {
        for (u32 index = 0; index < terminals->rows; ++index) {
            const auto terminal = terminals->RowByIndex(index);
            if (!terminal || terminals->Unsigned(*terminal, "seg_type") != 1) {
                continue;
            }
            MarkerRecord probe{};
            probe.info_id = terminals->Unsigned(*terminal, "fieldLoc_id");
            probe.open_flag = static_cast<u16>(terminals->Unsigned(*terminal, "flg_open"));
            probe.probe_id = static_cast<u16>(terminals->Unsigned(*terminal, "seg_id"));
            probe.kind = 3;
            probe.location_type = 5;
            probe.flags = 2U; // FrontierNav probe site.
            read_location(probe.info_id, probe);
            if (const auto formation = formations->RowById(probe.formation_id)) {
                probe.world_x = formations->Floating(*formation, "inner1_pos_x");
                probe.world_z = formations->Floating(*formation, "inner1_pos_z");
            }
            const auto duplicate = std::find_if(records.begin(), records.end(), [&](const auto& item) {
                return item.info_id == probe.info_id;
            });
            if (duplicate == records.end()) {
                records.emplace_back(std::move(probe));
            } else {
                const float existing_x = duplicate->world_x;
                const float existing_z = duplicate->world_z;
                *duplicate = std::move(probe);
                if (existing_x != 0.0F || existing_z != 0.0F) {
                    duplicate->world_x = existing_x;
                    duplicate->world_z = existing_z;
                }
            }
        }
    }

    std::string region_name;
    if (field_list && field_names) {
        for (u32 index = 0; index < field_list->rows; ++index) {
            const auto field = field_list->RowByIndex(index);
            if (!field || field_list->Unsigned(*field, "zoneNo") != zone) {
                continue;
            }
            const auto name = field_names->RowById(field_list->Unsigned(*field, "name"));
            if (name) {
                region_name = field_names->String(*name, "name");
            }
            break;
        }
    }

    std::vector<u8> output;
    output.reserve(80 + region_name.size() + records.size() * 64);
    XcxAppendU32(output, 0x50414D58); // XMAP
    XcxAppendU16(output, 2);
    XcxAppendU16(output, 80);
    XcxAppendU32(output, zone);
    XcxAppendFloat(output, static_cast<float>(bounds->Signed(*bounds_row, "left")));
    XcxAppendFloat(output, static_cast<float>(bounds->Signed(*bounds_row, "top")));
    XcxAppendFloat(output, static_cast<float>(bounds->Unsigned(*bounds_row, "width")));
    XcxAppendFloat(output, static_cast<float>(bounds->Unsigned(*bounds_row, "height")));
    XcxAppendFloat(output, static_cast<float>(bounds->Unsigned(*bounds_row, "size")));
    XcxAppendFloat(output, static_cast<float>(bounds->Unsigned(*bounds_row, "worldSize")));
    XcxAppendU32(output, bounds->Unsigned(*bounds_row, "maxScale"));
    XcxAppendU32(output, bounds->Unsigned(*bounds_row, "midScale"));
    XcxAppendU32(output, bounds->Unsigned(*bounds_row, "minScale"));
    XcxAppendFloat(output, static_cast<float>(bounds->Signed(*bounds_row, "startX")));
    XcxAppendFloat(output, static_cast<float>(bounds->Signed(*bounds_row, "startY")));
    XcxAppendU32(output, static_cast<u32>(bounds->Signed(*bounds_row, "move_top")));
    XcxAppendU32(output, static_cast<u32>(bounds->Signed(*bounds_row, "move_left")));
    XcxAppendU32(output, static_cast<u32>(bounds->Signed(*bounds_row, "move_bottom")));
    XcxAppendU32(output, static_cast<u32>(bounds->Signed(*bounds_row, "move_right")));
    XcxAppendU32(output, static_cast<u32>(records.size()));
    XcxAppendU32(output, static_cast<u32>(region_name.size()));
    output.insert(output.end(), region_name.begin(), region_name.end());
    for (const auto& record : records) {
        XcxAppendU32(output, record.info_id);
        XcxAppendU16(output, record.discovery_flag);
        XcxAppendU16(output, record.open_flag);
        XcxAppendU32(output, record.skip_travel_id);
        XcxAppendU32(output, record.formation_id);
        XcxAppendU16(output, record.probe_id);
        output.push_back(record.kind);
        output.push_back(record.icon);
        output.push_back(record.location_type);
        output.push_back(record.flags);
        XcxAppendU16(output, 0);
        XcxAppendFloat(output, record.world_x);
        XcxAppendFloat(output, record.world_z);
        XcxAppendFloat(output, record.offset_x);
        XcxAppendFloat(output, record.offset_y);
        XcxAppendU32(output, static_cast<u32>(record.name.size()));
        output.insert(output.end(), record.name.begin(), record.name.end());
    }
    return output;
}

std::vector<u8> LoadXcxPartyMetadata(const FileSys::VirtualDir& root) {
    const auto messages = LoadXcxBdat(root, "/bdat/us/common_ms.bdat", xcx_message_bdat);
    const auto names = messages ? FindXcxBdatTable(*messages, "DEF_PcList_ms") : std::nullopt;
    if (!names) {
        return {};
    }
    struct PartyName {
        u16 id;
        std::string name;
    };
    std::vector<PartyName> records;
    for (u32 id = 1; id <= 23; ++id) {
        const auto row = names->RowById(id);
        std::string name = row ? names->String(*row, "name") : std::string{};
        if (name.empty()) {
            continue;
        }
        records.push_back({static_cast<u16>(id), std::move(name)});
    }
    std::vector<u8> output;
    XcxAppendU32(output, 0x54504358); // XCPT
    XcxAppendU16(output, 1);
    XcxAppendU16(output, 12);
    XcxAppendU32(output, static_cast<u32>(records.size()));
    for (const auto& record : records) {
        XcxAppendU16(output, record.id);
        XcxAppendU16(output, static_cast<u16>(record.name.size()));
        output.insert(output.end(), record.name.begin(), record.name.end());
    }
    return output;
}

std::vector<u8> LoadXcxMissionMetadata(const FileSys::VirtualDir& root) {
    const auto common = LoadXcxBdat(root, "/bdat/common.bdat", xcx_common_bdat);
    const auto messages = LoadXcxBdat(root, "/bdat/us/common_ms.bdat", xcx_message_bdat);
    if (!common || !messages) {
        return {};
    }
    const auto quests = FindXcxBdatTable(*common, "FLD_questlist");
    const auto quest_text = FindXcxBdatTable(*messages, "quest_ms");
    if (!quests || !quest_text) {
        return {};
    }
    struct MissionRecord {
        u32 id;
        u16 flag;
        u8 category;
        std::string title;
        std::string summary;
    };
    std::vector<MissionRecord> records;
    records.reserve(512);
    for (u32 index = 0; index < quests->rows; ++index) {
        const auto row = quests->RowByIndex(index);
        if (!row || quests->Unsigned(*row, "prt_quest_id") != 0) {
            continue;
        }
        const u16 title_id = static_cast<u16>(quests->Unsigned(*row, "quest_title"));
        const u16 summary_id = static_cast<u16>(quests->Unsigned(*row, "summary"));
        const u16 flag = static_cast<u16>(quests->Unsigned(*row, "flg_type_a"));
        const u8 category = static_cast<u8>(quests->Unsigned(*row, "category"));
        const auto title_row = quest_text->RowById(title_id);
        const auto summary_row = quest_text->RowById(summary_id);
        std::string title = title_row ? quest_text->String(*title_row, "name") : std::string{};
        std::string summary = summary_row ? quest_text->String(*summary_row, "name") : std::string{};
        if (flag == 0 || category == 0 || title.empty()) {
            continue;
        }
        if (title.size() > std::numeric_limits<u16>::max() ||
            summary.size() > std::numeric_limits<u16>::max()) {
            continue;
        }
        records.push_back({quests->base_id + index, flag, category, std::move(title),
                           std::move(summary)});
    }
    std::vector<u8> output;
    XcxAppendU32(output, 0x54535158); // XQST
    XcxAppendU16(output, 1);
    XcxAppendU16(output, 12);
    XcxAppendU32(output, static_cast<u32>(records.size()));
    for (const auto& record : records) {
        XcxAppendU32(output, record.id);
        XcxAppendU16(output, record.flag);
        output.push_back(record.category);
        output.push_back(0);
        XcxAppendU16(output, static_cast<u16>(record.title.size()));
        XcxAppendU16(output, static_cast<u16>(record.summary.size()));
        output.insert(output.end(), record.title.begin(), record.title.end());
        output.insert(output.end(), record.summary.begin(), record.summary.end());
    }
    return output;
}

u32 XcxBlockHeightLog2(u32 block_rows) {
    const u32 height_and_half = block_rows + block_rows / 2;
    if (height_and_half >= 128) return 4;
    if (height_and_half >= 64) return 3;
    if (height_and_half >= 32) return 2;
    if (height_and_half >= 16) return 1;
    return 0;
}

std::vector<u8> DecodeXcxWilayPng(std::span<const u8> source) {
    if (source.size() < 0x40 || !HasMagic(source, "LAHD")) {
        return {};
    }
    const u32 textures_base = ReadU32(source, 0x24);
    if (textures_base > source.size() || source.size() - textures_base < 8) {
        return {};
    }
    const u32 array_offset = ReadU32(source, textures_base);
    const u32 texture_count = ReadU32(source, textures_base + 4);
    const u64 entry = static_cast<u64>(textures_base) + array_offset;
    if (texture_count == 0 || texture_count > 256 || entry > source.size() ||
        source.size() - entry < 12) {
        return {};
    }
    const u32 mibl_offset = ReadU32(source, entry + 4);
    const u32 mibl_size = ReadU32(source, entry + 8);
    const u64 mibl_start = static_cast<u64>(textures_base) + mibl_offset;
    if (mibl_size < 40 || mibl_start > source.size() || mibl_size > source.size() - mibl_start) {
        return {};
    }
    const auto mibl = source.subspan(static_cast<std::size_t>(mibl_start), mibl_size);
    const std::size_t footer = mibl.size() - 40;
    if (!HasMagic(mibl.subspan(footer + 36), "LBIM")) {
        return {};
    }
    const u32 image_size = ReadU32(mibl, footer);
    const u32 width = ReadU32(mibl, footer + 8);
    const u32 height = ReadU32(mibl, footer + 12);
    const u32 depth = ReadU32(mibl, footer + 16);
    const u32 image_format = ReadU32(mibl, footer + 24);
    if (width == 0 || height == 0 || width > MaxTextureDimension ||
        height > MaxTextureDimension || static_cast<u64>(width) * height > MaxTexturePixels ||
        depth != 1 || image_format != 77 || image_size > footer) {
        return {};
    }
    constexpr u32 block_width = 4;
    constexpr u32 block_height = 4;
    constexpr u32 bytes_per_block = 16;
    const u32 blocks_x = DivCeil(width, block_width);
    const u32 blocks_y = DivCeil(height, block_height);
    const u32 block_height_log2 = XcxBlockHeightLog2(blocks_y);
    const std::size_t required = Tegra::Texture::CalculateSize(
        true, bytes_per_block, blocks_x, blocks_y, 1, block_height_log2, 0);
    const std::size_t linear_size = static_cast<std::size_t>(blocks_x) * blocks_y *
                                    bytes_per_block;
    if (required > image_size || required > mibl.size()) {
        return {};
    }
    std::vector<u8> linear(linear_size);
    Tegra::Texture::UnswizzleTexture(linear, mibl.first(required), bytes_per_block,
                                     blocks_x, blocks_y, 1, block_height_log2, 0);
    std::vector<u8> rgba(static_cast<std::size_t>(width) * height * 4);
    for (u32 block_y = 0; block_y < blocks_y; ++block_y) {
        for (u32 block_x = 0; block_x < blocks_x; ++block_x) {
            const std::size_t block = (static_cast<std::size_t>(block_y) * blocks_x + block_x) *
                                      bytes_per_block;
            u8* destination = rgba.data() +
                              (static_cast<std::size_t>(block_y) * 4 * width + block_x * 4) * 4;
            bcn::DecodeBc7(linear.data() + block, destination, block_x * 4, block_y * 4,
                           width, height);
        }
    }
    TextureInfo descriptor{};
    descriptor.width = width;
    descriptor.height = height;
    return EncodePng(descriptor, rgba);
}

std::vector<u8> LoadXcxAsset(const FileSys::VirtualDir& root,
                             std::string_view logical_path) {
    const auto cached = xcx_asset_cache.find(std::string(logical_path));
    if (cached != xcx_asset_cache.end()) {
        return cached->second;
    }
    if (logical_path == "xcx/missions/data.bin") {
        auto data = LoadXcxMissionMetadata(root);
        if (!data.empty()) {
            xcx_asset_cache.emplace(std::string(logical_path), data);
        }
        return data;
    }
    if (logical_path == "xcx/party/data.bin") {
        auto data = LoadXcxPartyMetadata(root);
        if (!data.empty()) {
            xcx_asset_cache.emplace(std::string(logical_path), data);
        }
        return data;
    }
    constexpr std::string_view MapMetadataPrefix = "xcx/maps/meta_";
    if (logical_path.starts_with(MapMetadataPrefix) && logical_path.ends_with(".bin")) {
        const auto zone_text = logical_path.substr(
            MapMetadataPrefix.size(), logical_path.size() - MapMetadataPrefix.size() - 4);
        if (zone_text.empty() || zone_text.size() > 5 ||
            !std::all_of(zone_text.begin(), zone_text.end(), [](char value) {
                return std::isdigit(static_cast<unsigned char>(value));
            })) {
            return {};
        }
        u32 zone{};
        for (const char value : zone_text) {
            zone = zone * 10 + static_cast<u32>(value - '0');
        }
        auto data = LoadXcxMapMetadata(root, zone);
        if (!data.empty()) {
            xcx_asset_cache.emplace(std::string(logical_path), data);
        }
        return data;
    }
    if (!logical_path.starts_with("xcx/maps/map") || !logical_path.ends_with(".png")) {
        return {};
    }
    const std::string_view name = logical_path.substr(9, logical_path.size() - 9 - 4);
    if (name.size() < 8 || name.size() > 20 || name[0] != 'm' || name[1] != 'a' ||
        name[2] != 'p' || !std::all_of(name.begin() + 3, name.end(), [](char value) {
            return std::isdigit(static_cast<unsigned char>(value)) || value == '_';
        })) {
        return {};
    }
    const auto member = ReadXcxArchiveMember(root, "/ui/image/" + std::string(name) + ".wilay");
    auto png = member ? DecodeXcxWilayPng(*member) : std::vector<u8>{};
    if (!png.empty()) {
        xcx_asset_cache.emplace(std::string(logical_path), png);
    }
    return png;
}

}

std::vector<u8> LoadAsset(EmulationSession& session, std::string_view logical_path) {
    u64 title_id{};
    FileSys::VirtualDir root;
    {
        [[maybe_unused]] auto system_access = session.LockSystemAccess();
        if (!session.IsRunning()) {
            return {};
        }
        title_id = session.System().GetApplicationProcessProgramID();
        if (title_id != BotwTitleId && title_id != Mk8dTitleId &&
            title_id != SshdTitleId && title_id != XcxTitleId && title_id != SmoCompanion::TitleId) {
            return {};
        }
        if (title_id == SmoCompanion::TitleId &&
            session.System().GetApplicationProcessBuildID() != SmoCompanion::BuildId) return {};
        root = GetRomfsRoot(session, title_id);
    }
    if (!root) {
        return {};
    }
    std::scoped_lock lock(asset_mutex);
    if (title_id == SmoCompanion::TitleId) {
        if (const auto stage = LogicalComponent(logical_path, "smo/stage/", ".msbt")) {
            if (!smo_stage_messages) {
                const auto source = ReadRomfsFile(root,
                    "/LocalizedData/USen/MessageData/StageMessage.szs", MaxArchiveSize);
                if (!source) return {};
                smo_stage_messages = DecompressYaz0(*source);
            }
            if (!smo_stage_messages) return {};
            auto file = ExtractSarcFile(*smo_stage_messages, *stage + ".msbt");
            return file && file->size() <= 512 * 1024 ? std::move(*file) : std::vector<u8>{};
        }
        if (logical_path == "smo/messages/StageName.msbt" ||
            logical_path == "smo/messages/ItemCloth.msbt" ||
            logical_path == "smo/messages/ItemCap.msbt" ||
            logical_path == "smo/messages/ItemGift.msbt" ||
            logical_path == "smo/messages/ItemSticker.msbt" ||
            logical_path == "smo/messages/HackList.msbt" ||
            logical_path == "smo/messages/CollectBgmList.msbt") {
            const auto cached = smo_asset_cache.find(std::string(logical_path));
            if (cached != smo_asset_cache.end()) return cached->second;
            const auto source = ReadRomfsFile(root,
                "/LocalizedData/USen/MessageData/SystemMessage.szs", MaxArchiveSize);
            if (!source) return {};
            const auto archive = DecompressYaz0(*source);
            if (!archive) return {};
            const auto name = LogicalComponent(logical_path, "smo/messages/", ".msbt");
            if (!name) return {};
            auto message = ExtractSarcFile(*archive, *name + ".msbt");
            if (!message || message->size() > 512 * 1024) return {};
            smo_asset_cache.emplace(std::string(logical_path), *message);
            return *message;
        }
        // Extract each small static icon archive once, on the UI asset worker.
        // Bounded PNG cache is independent of the kingdom-map cache.
        for (const auto& [prefix, archive_name] :
             std::array<std::pair<std::string_view, std::string_view>, 4>{{
                 {"smo/icons/clothes/", "TextureClothes"},
                 {"smo/icons/souvenirs/", "TextureSouvenir"},
                 {"smo/icons/stickers/", "TextureSticker"},
                 {"smo/icons/captures/", "HackListTexture"}}}) {
            const auto icon = LogicalComponent(logical_path, prefix, ".png");
            if (!icon) continue;
            const auto key = std::string(logical_path);
            const auto cached = smo_icon_cache.find(key);
            if (cached != smo_icon_cache.end()) return cached->second;
            const auto archive_key = std::string(archive_name);
            auto it = smo_icon_archives.find(archive_key);
            if (it == smo_icon_archives.end()) {
                const auto source = ReadRomfsFile(root, "/ObjectData/" + archive_key + ".szs", 16 * 1024 * 1024);
                if (!source) return {};
                const auto archive = DecompressYaz0(*source);
                if (!archive) return {};
                const auto bfres = ExtractSarcFile(*archive, archive_key + ".bfres");
                if (!bfres || bfres->size() > 16 * 1024 * 1024) return {};
                it = smo_icon_archives.emplace(archive_key, *bfres).first;
            }
            auto png = DecodeTexturePng(it->second, *icon);
            if (!png.empty()) {
                if (smo_icon_cache.size() >= 48) smo_icon_cache.clear();
                smo_icon_cache.emplace(key, png);
            }
            return png;
        }
        const auto texture = LogicalComponent(logical_path, "smo/maps/", ".png");
        if (!texture) return {};
        const auto cached = smo_asset_cache.find(std::string(logical_path));
        if (cached != smo_asset_cache.end()) return cached->second;
        if (!smo_map_archive) {
            const auto source = ReadRomfsFile(root, "/ObjectData/Texture2dMap.szs", MaxArchiveSize);
            if (!source) return {};
            const auto archive = DecompressYaz0(*source);
            if (!archive) return {};
            smo_map_archive = ExtractSarcFile(*archive, "Texture2dMap.bfres");
        }
        if (!smo_map_archive) return {};
        auto png = DecodeTexturePng(*smo_map_archive, *texture);
        if (!png.empty()) {
            // Bounded decoded cache; the UI also releases its prior kingdom bitmap.
            if (smo_asset_cache.size() >= 3) smo_asset_cache.clear();
            smo_asset_cache.emplace(std::string(logical_path), png);
        }
        return png;
    }
    if (title_id == BotwTitleId) {
        if (logical_path == "botw/ui/hyrule_map.png") {
            if (botw_map_cache.empty()) {
                if (std::chrono::steady_clock::now() < botw_map_retry_after) {
                    return {};
                }
                auto map = LoadBotwMap(root);
                if (!map.empty()) {
                    botw_map_cache = std::move(map);
                    botw_map_retry_after = {};
                } else {
                    botw_map_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
                }
            }
            return botw_map_cache;
        }
        if (const auto actor = LogicalComponent(logical_path, "botw/stock/", ".png")) {
            return LoadBotwStock(root, *actor);
        }
        if (const auto rune = LogicalComponent(logical_path, "botw/ui/rune_", ".png")) {
            static const std::unordered_map<std::string, std::string> Actors{
                {"bombs", "Obj_RemoteBombCube"}, {"camera", "Obj_Camera"},
                {"cryonis", "Obj_IceMaker"}, {"magnesis", "Obj_Magnetglove"},
                {"stasis", "Obj_StopTimer"},
            };
            const auto actor = Actors.find(*rune);
            return actor != Actors.end() ? LoadBotwStock(root, actor->second) : std::vector<u8>{};
        }
        return LoadBotwCommon(root, logical_path);
    }
    if (title_id == Mk8dTitleId) {
        if (logical_path.starts_with("mk8d/characters/")) {
            return LoadMk8dCharacter(root, logical_path);
        }
        if (logical_path.starts_with("mk8d/items/")) {
            return LoadMk8dItem(root, logical_path);
        }
        if (logical_path.ends_with(".mapcamera")) {
            return LoadMk8dMapCamera(root, logical_path);
        }
        if (logical_path.starts_with("mk8d/maps/")) {
            return LoadMk8dMap(root, logical_path);
        }
        return LoadMk8dCommon(root, logical_path);
    }

    if (title_id == SshdTitleId) {
        return LoadSshdAsset(root, logical_path);
    }
    if (title_id == XcxTitleId) {
        return LoadXcxAsset(root, logical_path);
    }
    return {};
}

std::string GetRevision(EmulationSession& session) {
    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    if (!session.IsRunning()) {
        return {};
    }
    auto& system = session.System();
    const u64 title_id = system.GetApplicationProcessProgramID();
    if (title_id != BotwTitleId && title_id != Mk8dTitleId &&
        title_id != SshdTitleId && title_id != XcxTitleId && title_id != SmoCompanion::TitleId) {
        return {};
    }
    const FileSys::PatchManager patch_manager{
        title_id, system.GetFileSystemController(), system.GetContentProvider()};
    const auto version = patch_manager.GetGameVersion();
    return std::to_string(title_id) + "-" + (version ? std::to_string(*version) : "base");
}

uint64_t ExternalFileSize(EmulationSession& session, const char* path) {
    if (!path || !session.IsRunning()) return 0;
    const std::string_view name(path);
    if (name.size() > 512 || !name.starts_with("/") || name.find("..") != name.npos ||
        name.find('\\') != name.npos) return 0;
    const auto root = GetRomfsRoot(session, session.System().GetApplicationProcessProgramID());
    const auto file = root ? root->GetFileRelative(std::string(name)) : nullptr;
    return file ? file->GetSize() : 0;
}
size_t ExternalReadFile(EmulationSession& session, const char* path, uint64_t offset, void* out, size_t size) {
    const auto length = ExternalFileSize(session, path);
    if (!out || !length || offset > length || size > length - offset || size > MaxArchiveSize) return 0;
    const auto root = GetRomfsRoot(session, session.System().GetApplicationProcessProgramID());
    const auto file = root ? root->GetFileRelative(path) : nullptr;
    return file ? file->Read(static_cast<u8*>(out), size, offset) : 0;
}
std::vector<u8> ExternalDecodeTexture(std::span<const u8> source, std::string_view name, bool first) {
    if (source.size() > MaxArchiveSize || name.size() > 512) return {};
    return DecodeTexturePng(source, name, first);
}

void Reset() {
    std::scoped_lock lock(asset_mutex, romfs_mutex);
    romfs_root.reset();
    romfs_title_id = 0;
    botw_common_cache.clear();
    botw_map_cache.clear();
    mk8d_character_cache.clear();
    mk8d_item_cache.clear();
    mk8d_common_cache.clear();
    sshd_asset_cache.clear();
    sshd_archive_cache.clear();
    xcx_asset_cache.clear();
    smo_asset_cache.clear();
    smo_map_archive.reset();
    smo_icon_archives.clear();
    smo_icon_cache.clear();
    smo_stage_messages.reset();
    xcx_common_bdat.reset();
    xcx_message_bdat.reset();
    mk8d_character_archive.reset();
    mk8d_item_archive.reset();
    botw_common_archive.reset();
    botw_common_loaded = false;
    botw_common_retry_after = {};
    botw_map_retry_after = {};
    mk8d_character_failed = false;
    mk8d_item_failed = false;
    mk8d_common_loaded = false;
    xcx_archive_index.clear();
    xcx_archive_store.reset();
    xcx_archive_loaded = false;
    xcx_archive_retry_after = {};
}

}

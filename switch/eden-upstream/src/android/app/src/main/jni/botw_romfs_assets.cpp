#include "jni/botw_romfs_assets.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
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

#include <bc_decoder.h>

#include "common/stb.h"
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
#include "video_core/textures/astc.h"
#include "video_core/textures/decoders.h"

namespace DualScreenRomfsAssets {
namespace {

constexpr u64 BotwTitleId = 0x01007EF00011E000ULL;
constexpr u64 Mk8dTitleId = 0x0100152000022000ULL;
constexpr u64 PlzaTitleId = 0x0100F43008C44000ULL;
constexpr u64 SshdTitleId = 0x01002DA013484000ULL;
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
std::unordered_map<std::string, std::vector<u8>> plza_texture_cache;
std::unordered_map<std::string, std::vector<u8>> sshd_asset_cache;
std::unordered_map<std::string, std::shared_ptr<std::vector<u8>>> sshd_archive_cache;
std::shared_ptr<std::vector<u8>> mk8d_character_archive;
std::shared_ptr<std::vector<u8>> mk8d_item_archive;
std::shared_ptr<std::vector<u8>> botw_common_archive;
bool botw_common_loaded{};
std::chrono::steady_clock::time_point botw_common_retry_after{};
std::chrono::steady_clock::time_point botw_map_retry_after{};
bool mk8d_character_failed{};
bool mk8d_item_failed{};
bool mk8d_common_loaded{};

struct TrinityPackRange {
    u64 offset;
    u64 size;
};

std::unordered_map<std::string, TrinityPackRange> plza_trinity_index;
FileSys::VirtualFile plza_trinity_store;
bool plza_trinity_loaded{};
std::chrono::steady_clock::time_point plza_trinity_retry_after{};

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
    static constexpr std::array<Mapping, 41> Assets{{
        {"sshd/raw/navi_table.dat", "/Object/NX/System.arc", "dat/navi_table.dat", false},
        {"sshd/raw/items.msbt", "/US/Object/en_US/0-Common.arc",
         "0-Common/003-ItemGet.msbt", false},
        {"sshd/raw/system.msbt", "/US/Object/en_US/0-Common.arc",
         "0-Common/002-System.msbt", false},
        {"sshd/raw/fi.msbt", "/US/Object/en_US/0-Common.arc",
         "0-Common/006-KenseiNormal.msbt", false},
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

u64 TrinityHash(std::string_view value) {
    u64 hash = 0xCBF29CE484222645ULL;
    for (const u8 byte : value) {
        hash ^= byte;
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

bool LoadPlzaTrinityIndex(const FileSys::VirtualDir& root) {
    if (plza_trinity_loaded) {
        return plza_trinity_store != nullptr;
    }
    if (std::chrono::steady_clock::now() < plza_trinity_retry_after) {
        return false;
    }
    plza_trinity_index.clear();
    plza_trinity_store.reset();
    const auto descriptor = ReadRomfsFile(root, "/arc/data.trpfd", 16 * 1024 * 1024);
    const auto store = root ? root->GetFileRelative("/arc/data.trpfs") : nullptr;
    if (!descriptor || !store || store->GetSize() < 0x40 || descriptor->size() < 0x40) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }

    bool valid{};
    const u32 descriptor_root = ReadU32(*descriptor, 0, &valid);
    if (!valid || descriptor_root < 4 || descriptor_root + 4 > descriptor->size()) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const u32 vtable_distance = ReadU32(*descriptor, descriptor_root, &valid);
    if (!valid || vtable_distance > descriptor_root) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const std::size_t descriptor_vtable = descriptor_root - vtable_distance;
    if (descriptor_vtable + 8 > descriptor->size()) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const u16 vtable_size = static_cast<u16>((*descriptor)[descriptor_vtable]) |
                            static_cast<u16>((*descriptor)[descriptor_vtable + 1] << 8);
    const u16 names_offset = static_cast<u16>((*descriptor)[descriptor_vtable + 6]) |
                             static_cast<u16>((*descriptor)[descriptor_vtable + 7] << 8);
    if (vtable_size < 8 || descriptor_vtable + vtable_size > descriptor->size() ||
        names_offset == 0 || descriptor_root + names_offset + 4 > descriptor->size()) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const std::size_t names_pointer = descriptor_root + names_offset;
    const u32 descriptor_relative = ReadU32(*descriptor, names_pointer, &valid);
    if (!valid || descriptor_relative > descriptor->size() - names_pointer) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const std::size_t descriptor_table = names_pointer + descriptor_relative;
    const u32 descriptor_count = ReadU32(*descriptor, descriptor_table, &valid);
    if (!valid || descriptor_count == 0 || descriptor_count > 100000 ||
        descriptor_table + 4 + static_cast<u64>(descriptor_count) * 4 > descriptor->size()) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    std::vector<std::pair<std::string, u64>> names;
    names.reserve(descriptor_count);
    for (u32 index = 0; index < descriptor_count; ++index) {
        const std::size_t pointer = descriptor_table + 4 + static_cast<std::size_t>(index) * 4;
        const u32 relative = ReadU32(*descriptor, pointer, &valid);
        if (!valid || relative > descriptor->size() - pointer) {
            continue;
        }
        const std::size_t entry = pointer + relative;
        const u32 length = ReadU32(*descriptor, entry, &valid);
        if (!valid || length == 0 || length > 1024 || entry + 4 + length > descriptor->size()) {
            continue;
        }
        std::string name(reinterpret_cast<const char*>(descriptor->data() + entry + 4), length);
        names.emplace_back(name, TrinityHash(name));
    }

    const auto store_header = store->ReadBytes(16, 0);
    if (store_header.size() != 16 ||
        !HasMagic(store_header, std::string_view{"ONEPACK\0", 8})) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const u64 map_offset = ReadU64(store_header, 8, &valid);
    if (!valid || map_offset > store->GetSize() || store->GetSize() - map_offset < 36) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const auto count_source = store->ReadBytes(4, static_cast<std::size_t>(map_offset + 28));
    const u32 pack_count = ReadU32(count_source, 0, &valid);
    if (!valid || pack_count == 0 || pack_count > 100000) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const u64 offsets_position = map_offset + 32;
    const u64 hashes_count_position = offsets_position + static_cast<u64>(pack_count) * 8 + 4;
    if (hashes_count_position + 4 > store->GetSize()) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const auto offset_source = store->ReadBytes(static_cast<std::size_t>(pack_count) * 8,
                                                static_cast<std::size_t>(offsets_position));
    const auto hash_count_source =
        store->ReadBytes(4, static_cast<std::size_t>(hashes_count_position));
    const u32 hash_count = ReadU32(hash_count_source, 0, &valid);
    if (!valid || hash_count != pack_count || offset_source.size() != pack_count * 8ULL) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const u64 hashes_position = hashes_count_position + 4;
    if (hashes_position + static_cast<u64>(hash_count) * 8 > store->GetSize()) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    const auto hash_source = store->ReadBytes(static_cast<std::size_t>(hash_count) * 8,
                                              static_cast<std::size_t>(hashes_position));
    if (hash_source.size() != hash_count * 8ULL) {
        plza_trinity_retry_after = std::chrono::steady_clock::now() + AssetRetryDelay;
        return false;
    }
    std::unordered_map<u64, u64> offsets;
    std::vector<u64> ordered_offsets;
    offsets.reserve(pack_count);
    ordered_offsets.reserve(pack_count);
    for (u32 index = 0; index < pack_count; ++index) {
        const u64 offset = ReadU64(offset_source, static_cast<std::size_t>(index) * 8);
        const u64 hash = ReadU64(hash_source, static_cast<std::size_t>(index) * 8);
        if (offset < store->GetSize()) {
            offsets.emplace(hash, offset);
            ordered_offsets.push_back(offset);
        }
    }
    std::sort(ordered_offsets.begin(), ordered_offsets.end());
    ordered_offsets.erase(std::unique(ordered_offsets.begin(), ordered_offsets.end()),
                          ordered_offsets.end());
    plza_trinity_index.reserve(names.size());
    for (const auto& [name, hash] : names) {
        const auto found = offsets.find(hash);
        if (found == offsets.end()) {
            continue;
        }
        const auto next = std::upper_bound(ordered_offsets.begin(), ordered_offsets.end(),
                                           found->second);
        const u64 end = next == ordered_offsets.end() ? store->GetSize() : *next;
        if (end > found->second) {
            plza_trinity_index.emplace(name,
                                       TrinityPackRange{found->second, end - found->second});
        }
    }
    plza_trinity_store = store;
    plza_trinity_loaded = true;
    plza_trinity_retry_after = {};
    return true;
}

std::vector<u8> LoadPlzaTrinityPack(const FileSys::VirtualDir& root,
                                    std::string_view pack_name) {
    if (!LoadPlzaTrinityIndex(root)) {
        return {};
    }
    const auto found = plza_trinity_index.find(std::string(pack_name));
    if (found == plza_trinity_index.end() || found->second.size == 0 ||
        found->second.size > MaxArchiveSize) {
        return {};
    }
    return plza_trinity_store->ReadBytes(static_cast<std::size_t>(found->second.size),
                                         static_cast<std::size_t>(found->second.offset));
}

std::vector<u8> LoadPlzaTexture(const FileSys::VirtualDir& root,
                                std::string_view logical_path) {
    const auto cached = plza_texture_cache.find(std::string(logical_path));
    if (cached != plza_texture_cache.end()) {
        return cached->second;
    }
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 5> UiPacks{{
        {"plza/ui/bag.png", "arc/uidatabagbag_top_00_eng.arc.trpak"},
        {"plza/ui/map.png", "arc/uidatamapmap_info_00_eng.arc.trpak"},
        {"plza/ui/team.png", "arc/uidatastatusstatus_top_00_eng.arc.trpak"},
        {"plza/ui/pokedex.png", "arc/uidatapokedexpokedex_top.arc.trpak"},
        {"plza/ui/quests.png", "arc/uidataquest_infoquest_info_order_00_eng.arc.trpak"},
    }};
    std::string pack_name;
    std::string texture_name;
    if (const auto found = std::find_if(UiPacks.begin(), UiPacks.end(), [&](const auto& entry) {
            return entry.first == logical_path;
        }); found != UiPacks.end()) {
        pack_name = found->second;
    }
    if (const auto pokemon = LogicalComponent(logical_path, "plza/pokemon/", ".png")) {
        const auto valid_digit = [&](std::size_t index) {
            return index < pokemon->size() &&
                   std::isdigit(static_cast<unsigned char>((*pokemon)[index])) != 0;
        };
        const bool valid_name = pokemon->size() == 17 && pokemon->starts_with("pm") &&
                                (*pokemon)[6] == '_' && (*pokemon)[9] == '_' &&
                                (*pokemon)[12] == '_' && (*pokemon)[15] == '_' &&
                                valid_digit(2) && valid_digit(3) && valid_digit(4) &&
                                valid_digit(5) && valid_digit(7) && valid_digit(8) &&
                                valid_digit(10) && valid_digit(11) && valid_digit(13) &&
                                valid_digit(14) && valid_digit(16);
        if (valid_name && LoadPlzaTrinityIndex(root)) {
            texture_name = *pokemon;
            const std::string species = pokemon->substr(0, 6);
            const std::string model = pokemon->substr(0, 12);
            const std::string prefix = "arc/ik_pokemondata" + species + model + "icon" +
                                       pokemon->substr(0, 16);
            const auto found = std::find_if(
                plza_trinity_index.begin(), plza_trinity_index.end(), [&](const auto& entry) {
                    return entry.first.starts_with(prefix) &&
                           entry.first.ends_with(".bntx.trpak");
                });
            if (found != plza_trinity_index.end()) {
                pack_name = found->first;
            }
        }
    }
    if (pack_name.empty()) {
        return {};
    }
    const auto pack = LoadPlzaTrinityPack(root, pack_name);
    auto png = pack.empty() ? std::vector<u8>{}
                            : DecodeTexturePng(pack, texture_name, texture_name.empty());
    if (!png.empty()) {
        plza_texture_cache.emplace(std::string(logical_path), png);
    }
    return png;
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
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 48> Textures{{
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
        if (title_id != BotwTitleId && title_id != Mk8dTitleId && title_id != PlzaTitleId &&
            title_id != SshdTitleId) {
            return {};
        }
        root = GetRomfsRoot(session, title_id);
    }
    if (!root) {
        return {};
    }
    std::scoped_lock lock(asset_mutex);
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
    if (title_id == PlzaTitleId) {
        return LoadPlzaTexture(root, logical_path);
    }
    if (title_id == SshdTitleId) {
        return LoadSshdAsset(root, logical_path);
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
    if (title_id != BotwTitleId && title_id != Mk8dTitleId && title_id != PlzaTitleId &&
        title_id != SshdTitleId) {
        return {};
    }
    const FileSys::PatchManager patch_manager{
        title_id, system.GetFileSystemController(), system.GetContentProvider()};
    const auto version = patch_manager.GetGameVersion();
    return std::to_string(title_id) + "-" + (version ? std::to_string(*version) : "base");
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
    plza_texture_cache.clear();
    sshd_asset_cache.clear();
    sshd_archive_cache.clear();
    mk8d_character_archive.reset();
    mk8d_item_archive.reset();
    botw_common_archive.reset();
    botw_common_loaded = false;
    botw_common_retry_after = {};
    botw_map_retry_after = {};
    mk8d_character_failed = false;
    mk8d_item_failed = false;
    mk8d_common_loaded = false;
    plza_trinity_index.clear();
    plza_trinity_store.reset();
    plza_trinity_loaded = false;
    plza_trinity_retry_after = {};
}

}

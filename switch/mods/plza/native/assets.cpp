// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_shim.h"
#include <chrono>
#include <optional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cctype>
namespace {
constexpr size_t MaxArchiveSize=64*1024*1024;
constexpr auto AssetRetryDelay=std::chrono::seconds{5};
namespace FileSys {
class File {
    std::string path;
public:
    explicit File(std::string name):path(std::move(name)){}
    uint64_t GetSize() { return PlzaHost->romfs_size(path.c_str()); }
    std::vector<u8> ReadBytes(size_t size,size_t offset) {
        if (size>MaxArchiveSize) return {};
        std::vector<u8> bytes(size);
        const auto read=PlzaHost->romfs_read(path.c_str(),offset,bytes.data(),size);
        if (read>size) return {};
        bytes.resize(read); return bytes;
    }
};
using VirtualFile=std::shared_ptr<File>;
class Directory {
public:
    VirtualFile GetFileRelative(const std::string& path) {
        auto file=std::make_shared<File>(path); return file->GetSize()?file:nullptr;
    }
};
using VirtualDir=std::shared_ptr<Directory>;
}
std::optional<std::vector<u8>> ReadRomfsFile(const FileSys::VirtualDir& root, std::string_view path,size_t max) {
    auto file=root->GetFileRelative(std::string(path));
    if (!file) return {};
    const auto size=file->GetSize();
    if (!size || size>max) return {};
    auto bytes=file->ReadBytes(size,0);
    if (bytes.size()!=size) return {};
    return bytes;
}
std::vector<u8> DecodeTexturePng(std::span<const u8> bytes,std::string_view name,bool first) {
    size_t size=0;
    std::string texture(name);
    auto* data=PlzaHost->decode_texture(bytes.data(),bytes.size(),texture.c_str(),first,&size);
    if (!data) return {};
    std::unique_ptr<u8,void(*)(void*)> owner(data,PlzaHost->free_buffer);
    if (size>8*1024*1024) return {};
    return {data,data+size};
}
struct TrinityPackRange { u64 offset, size; };
std::unordered_map<std::string, std::vector<u8>> plza_texture_cache;
std::unordered_map<std::string, TrinityPackRange> plza_trinity_index;
FileSys::VirtualFile plza_trinity_store;
bool plza_trinity_loaded{};
std::chrono::steady_clock::time_point plza_trinity_retry_after{};
std::mutex asset_mutex;
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
bool HasMagic(std::span<const u8> data, std::string_view magic) {
    return data.size() >= magic.size() &&
           std::memcmp(data.data(), magic.data(), magic.size()) == 0;
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
}
void ResetPlzaAssets() {
    const std::scoped_lock lock{asset_mutex};
    plza_texture_cache.clear(); plza_trinity_index.clear(); plza_trinity_store.reset();
    plza_trinity_loaded=false; plza_trinity_retry_after={};
}
size_t PlzaAsset(const char* path,uint8_t* out,size_t capacity) {
    static u64 process{};
    const std::scoped_lock lock{asset_mutex};
    try {
        if (!path) return 0;
        DsSessionInfo info{}; info.size=sizeof(info);
        if (!PlzaHost->session(PlzaHost->context,&info) || !info.running || info.title_id!=0x0100F43008C44000ULL) return 0;
        if (process!=info.process_id) {
            process=info.process_id;
            plza_texture_cache.clear(); plza_trinity_index.clear(); plza_trinity_store.reset();
            plza_trinity_loaded=false; plza_trinity_retry_after={};
        }
        auto png=LoadPlzaTexture(std::make_shared<FileSys::Directory>(),path);
        if (!out) return png.size();
        if (capacity<png.size()) return 0;
        std::memcpy(out,png.data(),png.size()); return png.size();
    } catch (...) { PlzaHost->log(2,"[PLZA] external texture load failed"); return 0; }
}

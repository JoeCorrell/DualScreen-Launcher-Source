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

std::optional<std::vector<u8>> LoadNestedArchive(int root,
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

std::vector<u8> LoadBotwStock(int root, std::string_view actor) {
    const auto source = ReadRomfsFile(root, "/UI/StockItem/" + std::string(actor) + ".sbitemico",
                                      MaxSourceSize);
    if (!source) {
        return {};
    }
    const auto decoded = DecompressYaz0(*source);
    return decoded ? DecodeTexturePng(*decoded, actor, true) : std::vector<u8>{};
}

std::vector<u8> LoadBotwCommon(int root, std::string_view logical_path) {
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

std::vector<u8> LoadBotwMap(int root) {
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


std::vector<u8> LoadBotwAsset(std::string_view logical_path) {
 const auto title_id=BotwTitleId; const int root=0;
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
return {};
}

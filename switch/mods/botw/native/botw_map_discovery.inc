#include "botw_map_flags.inc"

// GameData scalar buffers, typed by the native schema. Region geometry is read
// from the live Ecosystem::EcoMapInfo, not reconstructed from tower locations.
// Supported-build evidence: 1185744..1185758 initializes EcoMapInfo at +1F0;
// 11858A8 queries its BECO runs; 109D228..109D278 uses area+1 -> MapTower_%02d.
std::string GetMapDiscovery(EmulationSession& session, const std::string& known_regions) {
    [[maybe_unused]] auto access = session.LockSystemAccess();
    constexpr auto unavailable = R"({"status":"unavailable"})";
    if (!session.IsRunning()) return unavailable;
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != BotwTitleId || !IsExpectedBuild(system))
        return unavailable;
    const auto [base, size] = system.GetApplicationProcessMainModule();
    if (!base || size <= GameDataManagerGotOffset) return unavailable;
    auto& memory = system.ApplicationMemory();
    auto ptr = [&](VAddr p) { return Read<u64>(memory, p).value_or(0); };
    const auto player_info = ptr(base + 0x1E13ED0);
    const auto player = player_info ? ptr(player_info + 0x60) : 0;
    if (!player || Read<u8>(memory, player + 0x64) != 1) return unavailable;
    const auto manager_storage = ptr(base + GameDataManagerGotOffset);
    const auto manager = manager_storage ? ptr(manager_storage) : 0;
    const auto holder = manager ? ptr(manager + GameDataFlagBufferHolderOffset) : 0;
    const auto buffer = holder ? ptr(holder) : 0;
    const auto count = buffer ? Read<u32>(memory, buffer + 8) : std::nullopt;
    const auto entries = buffer ? ptr(buffer + 0x10) : 0;
    const auto s32_count = buffer ? Read<u32>(memory, buffer + 0x18) : std::nullopt;
    const auto s32_entries = buffer ? ptr(buffer + 0x20) : 0;
    const auto generation = manager ? Read<u32>(memory, manager + GameDataFlagBufferGenerationOffset) : std::nullopt;
    if (!count || *count == 0 || *count > 100000 || !entries || !generation) return unavailable;
    if (!s32_count || *s32_count == 0 || *s32_count > 100000 || !s32_entries) return unavailable;
    const auto eco_storage = ptr(base + 0x1D66568);
    const auto eco = eco_storage ? ptr(eco_storage) : 0;
    const auto region = eco ? ptr(eco + 0x1F0) : 0;
    const auto header = region ? Read<std::array<u32,4>>(memory, region) : std::nullopt;
    if (!header || (*header)[0] != 0x00112233 || (*header)[1] < 2 ||
        (*header)[1] > 10000 || (*header)[2] == 0 || (*header)[2] > 1000) return unavailable;
    const auto process = system.ApplicationProcess();
    const auto process_id = process ? process->GetProcessId() : 0;
    const auto revision = std::to_string(process_id) + ":" + std::to_string(region);
    std::ostringstream out;
    out << R"({"status":"ok","revision":)" << JsonString(revision) << R"(,"visible":[)";
    bool comma = false;
    for (const auto& [name, target, type] : BotwMapFlags) {
        const bool integer = type == BotwMapFlagType::S32;
        const auto length = integer ? *s32_count : *count;
        const auto values = integer ? s32_entries : entries;
        const auto hash_at = [&](VAddr flag) -> std::optional<u32> {
            if (!flag) return std::nullopt;
            if (!integer) return Read<u32>(memory, flag + 0xC);
            const auto vtable = ptr(flag);
            const auto hash = vtable ? ResolveS32Getter(memory, flag, ptr(vtable + 0x10)) : std::nullopt;
            return hash ? std::optional<u32>{static_cast<u32>(*hash)} : std::nullopt;
        };
        u32 lo = 0, hi = length;
        while (lo < hi) {
            const auto mid = lo + (hi-lo)/2;
            const auto flag = ptr(values + mid*8ULL);
            const auto hash = hash_at(flag);
            if (!hash) return unavailable;
            if (*hash < target) lo = mid+1; else hi = mid;
        }
        if (lo == length) continue;
        const auto flag = ptr(values + lo*8ULL);
        if (!flag || hash_at(flag) != target) continue;
        bool visible;
        if (integer) {
            const auto vtable = ptr(flag);
            const auto value = vtable ? ResolveS32Getter(memory, flag, ptr(vtable + 0x88)) : std::nullopt;
            if (!value) return unavailable;
            visible = *value > 0;
        } else {
            const auto value = Read<u8>(memory, flag + 0xB);
            if (!value) return unavailable;
            visible = (*value & 1) != 0;
        }
        if (visible) {
            if (comma) out << ',';
            comma = true;
            out << JsonString(std::string{name});
        }
    }
    out << ']';
    // MapApp_MapIconNo / MapApp_MapIconPos are the saved pin/stamp arrays.
    // Native D86BDC..D86C34 rejects icon >44 and x<=-100000; D8B6B8 maps
    // 27..36 to stamps and 37..41 to the five pins. No map widget is queried.
    const auto find_array = [&](u64 offset, u32 hash) -> VAddr {
        const auto array_count = Read<u32>(memory, buffer + offset);
        const auto arrays = ptr(buffer + offset + 8);
        if (!array_count || *array_count > 1024 || !arrays) return 0;
        for (u32 i = 0; i < *array_count; ++i) {
            const auto array = ptr(arrays + i*8ULL);
            const auto length = array ? Read<u32>(memory, array) : std::nullopt;
            const auto values = array ? ptr(array + 8) : 0;
            const auto first = values ? ptr(values) : 0;
            const auto vtable = first ? ptr(first) : 0;
            if (!length || *length == 0 || *length > 512 || !vtable) continue;
            const auto actual = ResolveS32Getter(memory, first, ptr(vtable + 0x10));
            if (actual && static_cast<u32>(*actual) == hash) return array;
        }
        return 0;
    };
    const auto numbers = find_array(0xA8, 0x9383490EU);
    const auto positions = find_array(0x108, 0xEA9DEF3FU);
    out << R"(,"userMarkers":[)";
    comma = false;
    if (numbers && positions) {
        const auto number_count = Read<u32>(memory, numbers);
        const auto position_count = Read<u32>(memory, positions);
        const auto number_values = ptr(numbers + 8);
        const auto position_values = ptr(positions + 8);
        if (number_count && number_count == position_count && *number_count <= 512 &&
            number_values && position_values) {
            for (u32 i = 0; i < *number_count; ++i) {
                const auto number = ptr(number_values + i*8ULL);
                const auto position = ptr(position_values + i*8ULL);
                const auto nt = number ? ptr(number) : 0;
                const auto pt = position ? ptr(position) : 0;
                if (!nt || !pt) continue;
                const auto icon = ResolveS32Getter(memory, number, ptr(nt + 0x88));
                // D89B58 maps 27..35 and 37..41 to selectable stamps/pins;
                // 36 is an unused enum value, not a tenth stamp in the atlas.
                if (!icon || *icon < 27 || *icon > 41 || *icon == 36) continue;
                // getValueRef is evaluated with the existing bounded read-only getter
                // decoder; it supports Flag and FlagProxy without guessing value offsets.
                const auto address = ResolvePointerGetter(memory, position, ptr(pt + 0x90));
                const auto xyz = address ? Read<std::array<float,3>>(memory,*address) : std::nullopt;
                if (!xyz || !std::all_of(xyz->begin(),xyz->end(),[](float v){return std::isfinite(v);}) ||
                    (*xyz)[0] <= -100000 || std::abs((*xyz)[0]) > 6000 || std::abs((*xyz)[2]) > 6000)
                    continue;
                if (ResolveS32Getter(memory,number,ptr(nt + 0x88)) != icon ||
                    Read<std::array<float,3>>(memory,*address) != xyz) continue;
                if (comma) out << ',';
                comma = true;
                out << R"({"slot":)" << i << R"(,"icon":)" << *icon
                    << R"(,"x":)" << (*xyz)[0] << R"(,"z":)" << (*xyz)[2] << '}';
            }
        }
    }
    out << ']';
    if (known_regions != revision) {
        const u64 offsets_end = 0x10 + static_cast<u64>((*header)[1])*4;
        const auto last_offset = Read<u32>(memory, region + offsets_end - 4);
        if (!last_offset || *last_offset > 1000000) return unavailable;
        const u64 length = offsets_end + static_cast<u64>(*last_offset)*2;
        if (length > 2*1024*1024 || !memory.IsValidVirtualAddressRange(region,length)) return unavailable;
        std::vector<u8> bytes(length);
        if (!memory.ReadBlockUnsafe(region,bytes.data(),bytes.size())) return unavailable;
        out << R"(,"regions":)" << JsonString(Common::HexToString(std::span<const u8>{bytes}));
    }
    if (ptr(holder) != buffer || ptr(buffer + 0x10) != entries ||
        ptr(buffer + 0x20) != s32_entries || Read<u32>(memory, buffer + 0x18) != s32_count ||
        ptr(player_info + 0x60) != player || Read<u8>(memory, player + 0x64) != 1 ||
        Read<u32>(memory, manager + GameDataFlagBufferGenerationOffset) != generation ||
        ptr(eco + 0x1F0) != region) return unavailable;
    out << '}';
    return out.str();
}

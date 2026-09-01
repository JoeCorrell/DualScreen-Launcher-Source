#include "jni/arkham_companion.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/common_types.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/memory.h"
#include "jni/native.h"

namespace ArkhamCompanion {
namespace {

constexpr u64 TitleId = 0x0100E870163CA000ULL;
constexpr std::array<u8, 0x20> BuildId{
    0x62, 0x1E, 0xE6, 0x6A, 0x67, 0x43, 0xD7, 0x50,
    0xCB, 0x91, 0xE4, 0xE5, 0xE3, 0xC9, 0x4E, 0x43,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr VAddr GNamesOffset = 0x01C4BA70;
constexpr VAddr GlobalSearchStart = 0x01BC0000;
constexpr VAddr GlobalSearchEnd = 0x02AD2000;
constexpr u32 Magic = 0x4D4B5241;
constexpr size_t SkillCount = 20;
constexpr size_t GadgetCount = 16;

// Gadget identity traced from the game's own data rather than outside knowledge:
// BmGame.xxx declares one RoomCount<Gadget> IntProperty per gadget the challenge
// tracker recognises (exports 4094-4103), and an FGadgetIDs enum alongside
// RInventoryGadget/RInventoryManager. `token` is matched against the runtime FName
// of the equipped gadget object; `index` is the stable slot shared with the Kotlin
// companion (ArkhamCompanionView.GADGETS) and with gadget_levels/gadget_mask.
//
// Order is longest-token-first for the ambiguous "Batarang" family so that
// e.g. "ControllableBatarang" never matches the plain "Batarang" entry.
struct GadgetDefinition {
    std::string_view token;
    std::string_view room_count_property;
    int index;
};

constexpr std::array<GadgetDefinition, 10> GadgetTable{{
    {"ControllableBatarang", "RoomCountControllableBatarang", 5},
    {"MultiBatarang", "RoomCountMultiBatarang", 7},
    {"SonicBatarang", "RoomCountSonicBatarang", 6},
    {"ExplosiveGel", "RoomCountExplosiveGel", 2},
    {"LineLauncher", "RoomCountLineLauncher", 4},
    {"Detonator", "RoomCountDetonator", 8},
    {"Resonator", "RoomCountResonator", 3},
    {"BatClaw", "RoomCountBatClaw", 1},
    {"Grapple", "RoomCountGrapple", 9},
    {"Batarang", "RoomCountBatarang", 0},
}};

// Classes traced from the BmGame.xxx export table. These are resolved by name
// against the live GObjects array; nothing here is an address or an offset.
constexpr std::string_view PlayerPawnClassName = "RPawnPlayerBm";
constexpr std::string_view PlayerControllerClassName = "RPlayerControllerBm";
constexpr std::string_view InventoryManagerClassName = "RInventoryManager";
constexpr std::string_view InventoryGadgetClassName = "RInventoryGadget";

enum class Status : u32 {
    NotRunning = 0,
    WrongTitle = 1,
    UnsupportedBuild = 2,
    Resolving = 3,
    WaitingForSave = 4,
    Active = 5,
};

enum Availability : u32 {
    StatsAvailable = 1U << 0,
    XpAvailable = 1U << 1,
    SkillsAvailable = 1U << 2,
    ObjectiveAvailable = 1U << 3,
    MapAvailable = 1U << 4,
    GadgetsAvailable = 1U << 5,
};

#pragma pack(push, 1)
struct SnapshotFrame {
    u32 magic;
    u16 version;
    u16 size;
    u32 sequence;
    u32 status;
    u32 availability;
    u32 stage;
    s32 health;
    s32 max_health;
    float health_percent;
    float xp;
    float xp_prev;
    float xp_next;
    float xp_over;
    float xp_level;
    s32 player_level;
    s32 story_percentage;
    s32 unlockables;
    s32 current_gadget;
    u32 gadget_mask;
    u32 upgrades_bought_count;
    float total_time;
    float player_x;
    float player_y;
    float player_z;
    float player_yaw;
    s32 location_id;
    s32 save_chapter;
    u32 gnames_count;
    u32 gobjects_count;
    u32 diagnostics;
    u8 gadget_levels[GadgetCount];
    u8 skill_states[SkillCount];
    u8 reserved[4];
    char objective[96];
    char description[160];
    char location[64];
};
#pragma pack(pop)

static_assert(sizeof(SnapshotFrame) == 480);

struct ArrayHeader {
    VAddr data{};
    s32 count{};
    s32 capacity{};
};

struct ObjectLayout {
    VAddr header_address{};
    VAddr objects{};
    s32 count{};
    s32 capacity{};
    u32 name_offset{};
    u32 class_offset{};
    u32 outer_offset{};
};

struct NameTable {
    VAddr data{};
    s32 count{};
    s32 capacity{};
    std::vector<VAddr> entries;
    std::unordered_map<u32, std::string> cache;
};

struct PropertyInfo {
    VAddr object{};
    std::string name;
    std::string type;
    u32 array_dim{1};
    u32 element_size{};
    u32 field_offset{};
};

struct RuntimeCache {
    std::mutex mutex;
    VAddr main_base{};
    u64 process_id{};
    u32 stage{};
    bool failed{};
    NameTable names;
    ObjectLayout layout;
    std::vector<VAddr> objects;
    std::unordered_set<VAddr> object_set;
    VAddr persistent_class{};
    VAddr persistent_object{};
    VAddr persistent_default{};
    u32 property_base{};
    u32 property_value_member{};
    u32 next_retry_sequence{};
    std::unordered_map<std::string, PropertyInfo> properties;

    // Live-gameplay reflection. The persistent object above is save data and only
    // changes at checkpoints, so anything that has to track Batman frame-to-frame
    // (position, facing, current health) is read from the live pawn instead.
    // Resolved once per process by class name; never a hardcoded offset.
    VAddr pawn_class{};
    VAddr pawn_object{};
    VAddr controller_class{};
    VAddr controller_object{};
    VAddr inventory_manager_class{};
    VAddr inventory_manager_object{};
    VAddr inventory_gadget_class{};
    // Location/Rotation are Engine-side Actor properties and Health/MaxHealth are
    // Pawn properties; neither is redeclared by RPawnPlayerBm, so they have to be
    // resolved against the base classes the pawn inherits from.
    VAddr actor_class{};
    VAddr pawn_base_class{};
    // Property tables keyed by owning class. `Location`/`Health`/`CurrentGadget`
    // are each declared by dozens of unrelated classes in BmGame.xxx, so a flat
    // name->property map is ambiguous; every lookup is scoped to its owner.
    std::unordered_map<VAddr, std::unordered_map<std::string, PropertyInfo>> class_properties;
    u32 live_refresh_sequence{};
    bool diagnostics_logged{};
};

RuntimeCache Cache;
std::atomic<u32> Sequence{0};

template <typename T>
std::optional<T> Read(Core::Memory::Memory& memory, VAddr address) {
    if (!address || !memory.IsValidVirtualAddressRange(address, sizeof(T))) {
        return std::nullopt;
    }
    T value{};
    if (!memory.ReadBlockUnsafe(address, &value, sizeof(value))) {
        return std::nullopt;
    }
    return value;
}

bool ReadBytes(Core::Memory::Memory& memory, VAddr address, void* output, size_t size) {
    return address && size && memory.IsValidVirtualAddressRange(address, size) &&
           memory.ReadBlockUnsafe(address, output, size);
}

bool IsExpectedBuild(const Core::System& system) {
    return system.GetApplicationProcessBuildID() == BuildId;
}

bool IsPrintableName(std::string_view value) {
    if (value.empty() || value.size() > 96) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte >= 0x20 && byte < 0x7F;
    });
}

std::string ReadName(Core::Memory::Memory& memory, RuntimeCache& cache, u32 index) {
    if (index >= static_cast<u32>(cache.names.count) || index >= cache.names.entries.size()) {
        return {};
    }
    if (const auto it = cache.names.cache.find(index); it != cache.names.cache.end()) {
        return it->second;
    }
    const VAddr entry = cache.names.entries[index];
    std::array<u8, 208> bytes{};
    if (!entry || !ReadBytes(memory, entry, bytes.data(), bytes.size())) {
        return {};
    }
    u32 header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    std::string result;
    if ((header & 1U) == 0) {
        for (size_t offset = 0x10; offset < bytes.size() && bytes[offset]; ++offset) {
            result.push_back(static_cast<char>(bytes[offset]));
        }
    } else {
        for (size_t offset = 0x10; offset + 1 < bytes.size(); offset += 2) {
            const u16 code = static_cast<u16>(bytes[offset] | (bytes[offset + 1] << 8));
            if (!code) {
                break;
            }
            if (code < 0x20 || code >= 0x7F) {
                result.clear();
                break;
            }
            result.push_back(static_cast<char>(code));
        }
    }
    if (!IsPrintableName(result)) {
        result.clear();
    }
    cache.names.cache.emplace(index, result);
    return result;
}

std::string ObjectName(Core::Memory::Memory& memory, RuntimeCache& cache, VAddr object) {
    const auto index = Read<u32>(memory, object + cache.layout.name_offset);
    return index ? ReadName(memory, cache, *index) : std::string{};
}

std::string ObjectClassName(Core::Memory::Memory& memory, RuntimeCache& cache, VAddr object) {
    const auto class_object = Read<VAddr>(memory, object + cache.layout.class_offset);
    return class_object && *class_object ? ObjectName(memory, cache, *class_object) : std::string{};
}

bool ResolveNameTable(Core::Memory::Memory& memory, VAddr main_base, RuntimeCache& cache) {
    const auto header = Read<ArrayHeader>(memory, main_base + GNamesOffset);
    if (!header || !header->data || header->count < 1000 || header->count > 500000 ||
        header->capacity < header->count || header->capacity > 1000000) {
        return false;
    }
    std::vector<VAddr> entries(static_cast<size_t>(header->count));
    if (!ReadBytes(memory, header->data, entries.data(), entries.size() * sizeof(VAddr))) {
        return false;
    }
    cache.names.data = header->data;
    cache.names.count = header->count;
    cache.names.capacity = header->capacity;
    cache.names.entries = std::move(entries);
    cache.names.cache.clear();
    return ReadName(memory, cache, 0) == "None";
}

struct HeaderCandidate {
    VAddr address{};
    ArrayHeader header{};
};

std::vector<HeaderCandidate> FindArrayCandidates(Core::Memory::Memory& memory, VAddr main_base,
                                                 VAddr main_size) {
    std::vector<HeaderCandidate> result;
    const VAddr end_offset = std::min<VAddr>(main_size, GlobalSearchEnd);
    if (end_offset <= GlobalSearchStart) {
        return result;
    }
    constexpr size_t ChunkSize = 512 * 1024;
    std::vector<u8> bytes(ChunkSize + sizeof(ArrayHeader));
    for (VAddr offset = GlobalSearchStart; offset < end_offset; offset += ChunkSize) {
        const size_t size = static_cast<size_t>(std::min<VAddr>(ChunkSize, end_offset - offset));
        if (!ReadBytes(memory, main_base + offset, bytes.data(), size)) {
            continue;
        }
        for (size_t local = 0; local + sizeof(ArrayHeader) <= size; local += 8) {
            ArrayHeader header{};
            std::memcpy(&header, bytes.data() + local, sizeof(header));
            if (!header.data || header.count < 4000 || header.count > 400000 ||
                header.capacity < header.count || header.capacity > 800000 ||
                (header.data & 7U) != 0 ||
                !memory.IsValidVirtualAddressRange(header.data, sizeof(VAddr))) {
                continue;
            }
            result.push_back({main_base + offset + local, header});
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.header.count > right.header.count;
    });
    result.erase(std::unique(result.begin(), result.end(), [](const auto& left, const auto& right) {
                     return left.header.data == right.header.data &&
                            left.header.count == right.header.count;
                 }),
                 result.end());
    if (result.size() > 40) {
        result.resize(40);
    }
    return result;
}

struct ObjectSample {
    VAddr address{};
    std::array<u8, 0x90> bytes{};
};

bool DiscoverObjectLayout(Core::Memory::Memory& memory, RuntimeCache& cache,
                          const HeaderCandidate& candidate, ObjectLayout& output,
                          std::vector<VAddr>& all_objects) {
    std::vector<VAddr> objects(static_cast<size_t>(candidate.header.count));
    if (!ReadBytes(memory, candidate.header.data, objects.data(), objects.size() * sizeof(VAddr))) {
        return false;
    }
    std::unordered_set<VAddr> object_set;
    object_set.reserve(objects.size());
    for (const VAddr object : objects) {
        if (object && (object & 7U) == 0 &&
            memory.IsValidVirtualAddressRange(object, 0x90)) {
            object_set.insert(object);
        }
    }
    if (object_set.size() < objects.size() / 3) {
        return false;
    }
    std::vector<ObjectSample> samples;
    samples.reserve(96);
    const size_t stride = std::max<size_t>(1, objects.size() / 160);
    for (size_t index = 0; index < objects.size() && samples.size() < 96; index += stride) {
        const VAddr object = objects[index];
        if (!object_set.contains(object)) {
            continue;
        }
        ObjectSample sample{};
        sample.address = object;
        if (ReadBytes(memory, object, sample.bytes.data(), sample.bytes.size())) {
            samples.push_back(sample);
        }
    }
    if (samples.size() < 24) {
        return false;
    }
    u32 best_name_offset{};
    int best_name_score{};
    for (u32 offset = 0x10; offset <= 0x70; offset += 4) {
        int valid{};
        std::unordered_set<u32> distinct;
        for (const auto& sample : samples) {
            u32 index{};
            u32 number{};
            std::memcpy(&index, sample.bytes.data() + offset, sizeof(index));
            std::memcpy(&number, sample.bytes.data() + offset + 4, sizeof(number));
            if (index == 0 || index >= static_cast<u32>(cache.names.count) || number > 100000) {
                continue;
            }
            const std::string name = ReadName(memory, cache, index);
            if (!name.empty()) {
                ++valid;
                distinct.insert(index);
            }
        }
        const int score = valid * 4 + static_cast<int>(distinct.size());
        if (score > best_name_score) {
            best_name_score = score;
            best_name_offset = offset;
        }
    }
    if (!best_name_offset || best_name_score < static_cast<int>(samples.size()) * 2) {
        return false;
    }
    u32 best_class_offset{};
    int best_class_score{};
    for (u32 offset = 0x08; offset <= 0x78; offset += 8) {
        if (offset == best_name_offset) {
            continue;
        }
        int valid{};
        std::unordered_set<VAddr> distinct;
        for (const auto& sample : samples) {
            VAddr pointer{};
            std::memcpy(&pointer, sample.bytes.data() + offset, sizeof(pointer));
            if (object_set.contains(pointer)) {
                ++valid;
                distinct.insert(pointer);
            }
        }
        const int score = valid * 8 - static_cast<int>(distinct.size() / 2);
        if (score > best_class_score) {
            best_class_score = score;
            best_class_offset = offset;
        }
    }
    if (!best_class_offset || best_class_score < static_cast<int>(samples.size()) * 5) {
        return false;
    }
    int semantic_score{};
    for (const auto& sample : samples) {
        VAddr class_object{};
        std::memcpy(&class_object, sample.bytes.data() + best_class_offset, sizeof(class_object));
        if (!class_object) {
            continue;
        }
        const auto index = Read<u32>(memory, class_object + best_name_offset);
        if (!index) {
            continue;
        }
        const std::string class_name = ReadName(memory, cache, *index);
        if (!class_name.empty()) {
            ++semantic_score;
        }
    }
    if (semantic_score < static_cast<int>(samples.size()) / 2) {
        return false;
    }
    output.header_address = candidate.address;
    output.objects = candidate.header.data;
    output.count = candidate.header.count;
    output.capacity = candidate.header.capacity;
    output.name_offset = best_name_offset;
    output.class_offset = best_class_offset;
    all_objects = std::move(objects);
    return true;
}

bool ResolveObjects(Core::Memory::Memory& memory, VAddr main_base, VAddr main_size,
                    RuntimeCache& cache) {
    const auto candidates = FindArrayCandidates(memory, main_base, main_size);
    for (const auto& candidate : candidates) {
        if (candidate.header.data == cache.names.data) {
            continue;
        }
        ObjectLayout layout{};
        std::vector<VAddr> objects;
        if (!DiscoverObjectLayout(memory, cache, candidate, layout, objects)) {
            continue;
        }
        cache.layout = layout;
        cache.objects = std::move(objects);
        cache.object_set.clear();
        cache.object_set.reserve(cache.objects.size());
        for (const VAddr object : cache.objects) {
            if (object) {
                cache.object_set.insert(object);
            }
        }
        LOG_INFO(Frontend,
                 "[Arkham Companion] Resolved objects: count {}, name +0x{:X}, class +0x{:X}",
                 cache.layout.count, cache.layout.name_offset, cache.layout.class_offset);
        return true;
    }
    return false;
}

bool RefreshObjects(Core::Memory::Memory& memory, RuntimeCache& cache) {
    const auto header = Read<ArrayHeader>(memory, cache.layout.header_address);
    if (!header || !header->data || header->count < cache.layout.count ||
        header->count > 400000 || header->capacity < header->count ||
        header->capacity > 800000) {
        return false;
    }
    const size_t previous_count = cache.objects.size();
    const bool data_changed = header->data != cache.layout.objects;
    if (data_changed) {
        std::vector<VAddr> objects(static_cast<size_t>(header->count));
        if (!ReadBytes(memory, header->data, objects.data(), objects.size() * sizeof(VAddr))) {
            return false;
        }
        cache.objects = std::move(objects);
        cache.object_set.clear();
        cache.object_set.reserve(cache.objects.size());
        for (const VAddr object : cache.objects) {
            if (object) {
                cache.object_set.insert(object);
            }
        }
    } else if (static_cast<size_t>(header->count) > previous_count) {
        const size_t added = static_cast<size_t>(header->count) - previous_count;
        cache.objects.resize(static_cast<size_t>(header->count));
        if (!ReadBytes(memory, header->data + previous_count * sizeof(VAddr),
                       cache.objects.data() + previous_count, added * sizeof(VAddr))) {
            cache.objects.resize(previous_count);
            return false;
        }
        for (size_t index = previous_count; index < cache.objects.size(); ++index) {
            if (cache.objects[index]) {
                cache.object_set.insert(cache.objects[index]);
            }
        }
    }
    cache.layout.objects = header->data;
    cache.layout.count = header->count;
    cache.layout.capacity = header->capacity;
    const size_t search_start = data_changed ? 0 : previous_count;
    for (size_t index = std::min(search_start, cache.objects.size());
         index < cache.objects.size(); ++index) {
        const VAddr object = cache.objects[index];
        if (!object || object == cache.persistent_default ||
            !memory.IsValidVirtualAddressRange(object, 0x90)) {
            continue;
        }
        const auto class_object = Read<VAddr>(memory, object + cache.layout.class_offset);
        if (class_object && *class_object == cache.persistent_class) {
            cache.persistent_object = object;
            LOG_INFO(Frontend,
                     "[Arkham Companion] Live persistent data discovered at object {} of {}",
                     index, cache.objects.size());
            return true;
        }
    }
    return cache.persistent_object != 0;
}

struct TargetObject {
    VAddr address{};
    std::string name;
    std::string class_name;
};

std::vector<TargetObject> CollectTargets(Core::Memory::Memory& memory, RuntimeCache& cache) {
    static const std::unordered_set<std::string> TargetNames{
        "RPersistentData", "Default__RPersistentData", "PlayerHealth", "XP",
        "XPLevel", "XPForNextUpgrade", "XPForPrevUpgrade", "XPOverUpgradeThreshold",
        "playerLevel", "StoryPercentage", "UnlockablesToSpend", "UpgradesBought",
        "TotalTimePlayed", "CurrentGadgetName", "PrimaryObjective", "Description",
        "PlayerLocation", "PlayerRotation", "LocationID", "SaveGameChapter",
    };
    std::vector<TargetObject> result;
    for (const VAddr object : cache.objects) {
        if (!object || !memory.IsValidVirtualAddressRange(object, 0x90)) {
            continue;
        }
        const std::string name = ObjectName(memory, cache, object);
        if (!TargetNames.contains(name) && name.find("PersistentData") == std::string::npos) {
            continue;
        }
        result.push_back({object, name, ObjectClassName(memory, cache, object)});
    }
    return result;
}

bool ResolvePersistentObjects(Core::Memory::Memory& memory, RuntimeCache& cache,
                              const std::vector<TargetObject>& targets) {
    for (const auto& target : targets) {
        if (target.name == "RPersistentData" && target.class_name == "Class") {
            cache.persistent_class = target.address;
            break;
        }
    }
    if (!cache.persistent_class) {
        return false;
    }
    for (const VAddr object : cache.objects) {
        const auto class_object = Read<VAddr>(memory, object + cache.layout.class_offset);
        if (!class_object || *class_object != cache.persistent_class) {
            continue;
        }
        const std::string name = ObjectName(memory, cache, object);
        if (name == "Default__RPersistentData") {
            cache.persistent_default = object;
        } else if (name != "RPersistentData" && name.find("Default__") != 0) {
            cache.persistent_object = object;
        }
    }
    return cache.persistent_default != 0;
}

u32 ExpectedElementSize(std::string_view type) {
    if (type == "IntProperty" || type == "FloatProperty" || type == "BoolProperty" ||
        type == "ByteProperty") {
        return 4;
    }
    if (type == "NameProperty" || type == "ObjectProperty") {
        return 8;
    }
    if (type == "StrProperty" || type == "ArrayProperty") {
        return 16;
    }
    if (type == "StructProperty") {
        return 12;
    }
    return 0;
}

bool MatchesElementSize(std::string_view type, u32 size) {
    const u32 expected = ExpectedElementSize(type);
    if (expected && size == expected) {
        return true;
    }
    if (type == "BoolProperty" && size == 1) {
        return true;
    }
    return type == "StructProperty" && size >= 8 && size <= 256;
}

bool ResolveOuterOffset(Core::Memory::Memory& memory, RuntimeCache& cache,
                        const std::vector<TargetObject>& targets) {
    std::array<int, 16> scores{};
    for (const auto& target : targets) {
        if (target.class_name.find("Property") == std::string::npos) {
            continue;
        }
        std::array<u8, 0x90> bytes{};
        if (!ReadBytes(memory, target.address, bytes.data(), bytes.size())) {
            continue;
        }
        for (u32 offset = 0x08; offset <= 0x78; offset += 8) {
            VAddr pointer{};
            std::memcpy(&pointer, bytes.data() + offset, sizeof(pointer));
            if (pointer == cache.persistent_class) {
                scores[offset / 8]++;
            }
        }
    }
    int best_score{};
    u32 best_offset{};
    for (u32 offset = 0x08; offset <= 0x78; offset += 8) {
        if (offset == cache.layout.class_offset) {
            continue;
        }
        if (scores[offset / 8] > best_score) {
            best_score = scores[offset / 8];
            best_offset = offset;
        }
    }
    cache.layout.outer_offset = best_offset;
    return best_offset && best_score >= 4;
}

bool IsPersistentPropertyName(std::string_view name) {
    static const std::unordered_set<std::string> names{
        "PlayerHealth", "XP", "XPLevel", "XPForNextUpgrade", "XPForPrevUpgrade",
        "XPOverUpgradeThreshold", "playerLevel", "StoryPercentage", "UnlockablesToSpend",
        "UpgradesBought", "TotalTimePlayed", "CurrentGadgetName", "PrimaryObjective",
        "Description", "PlayerLocation", "PlayerRotation", "LocationID", "SaveGameChapter",
    };
    return names.contains(std::string{name});
}

bool ResolvePropertyObjects(Core::Memory::Memory& memory, RuntimeCache& cache,
                            const std::vector<TargetObject>& targets) {
    cache.properties.clear();
    for (const auto& target : targets) {
        if (!IsPersistentPropertyName(target.name) ||
            target.class_name.find("Property") == std::string::npos) {
            continue;
        }
        const auto outer = Read<VAddr>(memory, target.address + cache.layout.outer_offset);
        if (!outer || *outer != cache.persistent_class) {
            continue;
        }
        cache.properties[target.name] = PropertyInfo{target.address, target.name,
                                                     target.class_name};
    }
    return cache.properties.size() >= 10;
}

bool PlausibleArray(Core::Memory::Memory& memory, VAddr address, s32 max_count) {
    const auto header = Read<ArrayHeader>(memory, address);
    return header && header->count >= 0 && header->count <= max_count &&
           header->capacity >= header->count && header->capacity <= max_count * 4 &&
           (header->count == 0 ||
            (header->data && memory.IsValidVirtualAddressRange(header->data, 2)));
}

int ScorePropertyMember(Core::Memory::Memory& memory, RuntimeCache& cache, u32 base,
                        u32 member) {
    int score{};
    std::unordered_set<u32> offsets;
    const VAddr source = cache.persistent_object ? cache.persistent_object : cache.persistent_default;
    std::unordered_map<std::string, u32> values;
    for (auto& [name, property] : cache.properties) {
        const auto value = Read<u32>(memory, property.object + base + member);
        if (!value || *value < 0x40 || *value > 0x20000) {
            continue;
        }
        values.emplace(name, *value);
        offsets.insert(*value);
        score += 3;
        if ((*value & 3U) == 0) {
            ++score;
        }
    }
    score += static_cast<int>(offsets.size()) * 2;
    const auto get = [&](const char* name) -> std::optional<u32> {
        const auto it = values.find(name);
        return it == values.end() ? std::nullopt : std::optional<u32>{it->second};
    };
    if (const auto offset = get("PlayerHealth")) {
        const auto value = Read<s32>(memory, source + *offset);
        if (value && *value >= 0 && *value <= 2000) score += 8;
        else score -= 10;
    }
    if (const auto offset = get("playerLevel")) {
        const auto value = Read<s32>(memory, source + *offset);
        if (value && *value >= 0 && *value <= 100) score += 5;
        else score -= 7;
    }
    if (const auto offset = get("StoryPercentage")) {
        const auto value = Read<s32>(memory, source + *offset);
        if (value && *value >= 0 && *value <= 100) score += 5;
        else score -= 7;
    }
    int finite_xp{};
    for (const char* name : {"XP", "XPLevel", "XPForNextUpgrade", "XPForPrevUpgrade",
                             "XPOverUpgradeThreshold"}) {
        if (const auto offset = get(name)) {
            const auto value = Read<float>(memory, source + *offset);
            if (value && std::isfinite(*value) && *value >= -1.0f && *value < 100000000.0f) {
                ++finite_xp;
            }
        }
    }
    score += finite_xp * 2;
    if (const auto offset = get("CurrentGadgetName")) {
        const auto index = Read<u32>(memory, source + *offset);
        if (index && *index < static_cast<u32>(cache.names.count)) score += 5;
    }
    if (const auto offset = get("PrimaryObjective")) {
        if (PlausibleArray(memory, source + *offset, 512)) score += 5;
    }
    if (const auto offset = get("Description")) {
        if (PlausibleArray(memory, source + *offset, 1024)) score += 5;
    }
    return score;
}

bool ResolvePropertyLayout(Core::Memory::Memory& memory, RuntimeCache& cache) {
    u32 best_base{};
    int best_base_score{};
    for (u32 base = 0x40; base <= 0xB0; base += 4) {
        int score{};
        for (auto& [name, property] : cache.properties) {
            const auto values = Read<std::array<u32, 2>>(memory, property.object + base);
            if (!values) {
                continue;
            }
            // values[0] is ElementSize and values[1] is the field offset. Scoring the
            // type match against values[0] is what makes this discriminating; the
            // previous code compared it against the offset, so the base could score
            // well for the wrong reason.
            if ((*values)[0] >= 1 && (*values)[0] <= 256 &&
                (*values)[1] >= 0x08 && (*values)[1] <= 0x20000) {
                ++score;
                if (MatchesElementSize(property.type, (*values)[0])) {
                    score += 5;
                }
            }
        }
        if (score > best_base_score) {
            best_base_score = score;
            best_base = base;
        }
    }
    const int required_base_score = 18;
    if (!best_base || best_base_score < required_base_score) {
        LOG_WARNING(Frontend,
                    "[Arkham Companion] Property base unresolved: candidate +0x{:X}, score {}/{}, {} fields",
                    best_base, best_base_score, required_base_score, cache.properties.size());
        return false;
    }
    u32 best_member{};
    int best_member_score{std::numeric_limits<int>::min()};
    for (u32 member = 0x10; member <= 0xC0; member += 4) {
        const int score = ScorePropertyMember(memory, cache, best_base, member);
        if (score > best_member_score) {
            best_member_score = score;
            best_member = member;
        }
    }
    const int required_member_score =
        std::max(30, static_cast<int>(cache.properties.size()) * 2);
    // Field offsets now come from property_base + 4 directly, so a weak score here is
    // no longer a reason to fail resolution — it only means this secondary probe found
    // nothing interesting. Kept as a signal in case the layout changes in a future build.
    if (!best_member || best_member_score < required_member_score) {
        LOG_INFO(Frontend,
                 "[Arkham/Reflect] Secondary property probe inconclusive (base +0x{:X} score {}, "
                 "candidate +0x{:X} score {}/{}) — not required for field offsets",
                 best_base, best_base_score, best_member, best_member_score,
                 required_member_score);
    }
    cache.property_base = best_base;
    cache.property_value_member = best_member;
    u32 resolved{};
    for (auto& [name, property] : cache.properties) {
        const auto header = Read<std::array<u32, 2>>(memory, property.object + best_base);
        if (!header || !MatchesElementSize(property.type, (*header)[0]) ||
            (*header)[1] < 0x08 || (*header)[1] > 0x20000) {
            property.field_offset = 0;
            continue;
        }
        property.element_size = (*header)[0];
        property.field_offset = (*header)[1];
        // See FillPropertyOffsets: ArrayDim is not in this pair, so static arrays are
        // treated as scalar until it is located. UpgradesBought therefore reports a
        // single entry rather than a wrong count read past the end of the object.
        property.array_dim = 1;
        ++resolved;
    }
    LOG_INFO(Frontend, "[Arkham/Reflect] Persistent layout: {}/{} fields resolved at base +0x{:X}",
             resolved, cache.properties.size(), best_base);
    LOG_INFO(Frontend,
             "[Arkham Companion] Reflection: outer +0x{:X}, property +0x{:X}/+0x{:X}, {} fields",
             cache.layout.outer_offset, cache.property_base, cache.property_value_member,
             cache.properties.size());
    return true;
}

bool ResolveRuntime(Core::Memory::Memory& memory, VAddr main_base, VAddr main_size,
                    RuntimeCache& cache) {
    if (cache.stage == 5 && !cache.properties.empty()) {
        if (!ResolvePropertyLayout(memory, cache)) {
            return false;
        }
        cache.stage = 6;
        return true;
    }
    cache.stage = 1;
    if (!ResolveNameTable(memory, main_base, cache)) {
        return false;
    }
    cache.stage = 2;
    if (!ResolveObjects(memory, main_base, main_size, cache)) {
        return false;
    }
    cache.stage = 3;
    const auto targets = CollectTargets(memory, cache);
    if (!ResolvePersistentObjects(memory, cache, targets)) {
        return false;
    }
    cache.stage = 4;
    if (!ResolveOuterOffset(memory, cache, targets) ||
        !ResolvePropertyObjects(memory, cache, targets)) {
        return false;
    }
    cache.stage = 5;
    if (!ResolvePropertyLayout(memory, cache)) {
        return false;
    }
    cache.stage = 6;
    return true;
}

template <typename T>
std::optional<T> ReadProperty(Core::Memory::Memory& memory, const RuntimeCache& cache,
                              VAddr object, const char* name) {
    const auto it = cache.properties.find(name);
    if (it == cache.properties.end() || !it->second.field_offset) {
        return std::nullopt;
    }
    return Read<T>(memory, object + it->second.field_offset);
}

std::string ReadFString(Core::Memory::Memory& memory, VAddr address) {
    const auto header = Read<ArrayHeader>(memory, address);
    if (!header || header->count <= 0 || header->count > 1024 || !header->data ||
        header->capacity < header->count || header->capacity > 4096) {
        return {};
    }
    const size_t count = static_cast<size_t>(header->count);
    std::vector<u16> wide(count);
    if (!ReadBytes(memory, header->data, wide.data(), wide.size() * sizeof(u16))) {
        return {};
    }
    std::string result;
    result.reserve(count);
    bool wide_valid = true;
    for (const u16 code : wide) {
        if (!code) break;
        if (code < 0x20 || code >= 0x7F) {
            wide_valid = false;
            break;
        }
        result.push_back(static_cast<char>(code));
    }
    if (!wide_valid || result.empty()) {
        result.clear();
        std::vector<u8> narrow(count);
        if (!ReadBytes(memory, header->data, narrow.data(), narrow.size())) {
            return {};
        }
        for (const u8 code : narrow) {
            if (!code) break;
            if (code < 0x20 || code >= 0x7F) return {};
            result.push_back(static_cast<char>(code));
        }
    }
    return result;
}

void CopyText(char* destination, size_t capacity, std::string_view source) {
    if (!capacity) return;
    const size_t count = std::min(capacity - 1, source.size());
    std::memcpy(destination, source.data(), count);
    destination[count] = 0;
}

int GadgetIndex(std::string_view name) {
    // GadgetTable is ordered longest-token-first within the Batarang family so
    // that ControllableBatarang/MultiBatarang/SonicBatarang are never swallowed
    // by the plain "Batarang" entry.
    for (const auto& gadget : GadgetTable) {
        if (name.find(gadget.token) != std::string_view::npos) {
            return gadget.index;
        }
    }
    return -1;
}

// A UClass is an object whose own class is named "Class". Matching on that keeps
// us from picking up a same-named instance or default object.
VAddr FindClassByName(Core::Memory::Memory& memory, RuntimeCache& cache, std::string_view name) {
    for (const VAddr object : cache.objects) {
        if (!object || !memory.IsValidVirtualAddressRange(object, 0x90)) {
            continue;
        }
        if (ObjectName(memory, cache, object).compare(name) != 0) {
            continue;
        }
        if (ObjectClassName(memory, cache, object) == "Class") {
            return object;
        }
    }
    return 0;
}

// property_base/property_value_member are properties of the UProperty layout in
// this binary, not of any one class, so once ResolvePropertyLayout has pinned
// them down from RPersistentData they apply to every UProperty in GObjects.
// UProperty layout in this build, established from a live RInventoryGadget dump:
//
//   property_base + 0x00 : ElementSize   (4 for int/float, 8 for FName, 16 for FString)
//   property_base + 0x04 : Offset        (byte offset of the field within the object)
//
// The gadget dump made this unambiguous — its int/float fields resolved to
// 740, 744, 748, 752, 756, 760 ... a contiguous 4-byte-spaced run, while the word
// previously treated as the offset (base + property_value_member) held unrelated
// noise. ArrayDim is not adjacent to these two and is handled separately below.
bool FillPropertyOffsets(Core::Memory::Memory& memory, const RuntimeCache& cache,
                         PropertyInfo& property) {
    const auto header = Read<std::array<u32, 2>>(memory, property.object + cache.property_base);
    if (!header) {
        return false;
    }
    const u32 element_size = (*header)[0];
    const u32 field_offset = (*header)[1];
    // Reject anything whose element size disagrees with what the property type must
    // be; that check is what separates a real UProperty from a coincidental match.
    if (!MatchesElementSize(property.type, element_size)) {
        return false;
    }
    if (field_offset < 0x08 || field_offset > 0x20000) {
        return false;
    }
    property.element_size = element_size;
    property.field_offset = field_offset;
    // Static-array dimension is not in this pair. Until it is located, treat every
    // property as scalar rather than inventing a count: a wrong ArrayDim would make
    // an array read walk off the end of the object.
    property.array_dim = 1;
    return true;
}

// Collect every UProperty whose Outer is `class_object`. Scoping by owner is
// mandatory: BmGame.xxx declares Location/Health/CurrentGadget on dozens of
// unrelated classes, so resolving those names globally would pick an arbitrary one.
const std::unordered_map<std::string, PropertyInfo>& ResolveClassProperties(
    Core::Memory::Memory& memory, RuntimeCache& cache, VAddr class_object,
    std::string_view debug_name) {
    static const std::unordered_map<std::string, PropertyInfo> empty;
    if (!class_object || !cache.property_base || !cache.layout.outer_offset) {
        return empty;
    }
    if (const auto it = cache.class_properties.find(class_object);
        it != cache.class_properties.end()) {
        return it->second;
    }
    std::unordered_map<std::string, PropertyInfo> table;
    u32 rejected{};
    for (const VAddr object : cache.objects) {
        if (!object || !memory.IsValidVirtualAddressRange(object, 0x90)) {
            continue;
        }
        const auto outer = Read<VAddr>(memory, object + cache.layout.outer_offset);
        if (!outer || *outer != class_object) {
            continue;
        }
        const std::string class_name = ObjectClassName(memory, cache, object);
        if (class_name.find("Property") == std::string::npos) {
            continue;
        }
        PropertyInfo property{object, ObjectName(memory, cache, object), class_name};
        if (property.name.empty() || !FillPropertyOffsets(memory, cache, property)) {
            ++rejected;
            continue;
        }
        table.emplace(property.name, property);
    }
    LOG_INFO(Frontend, "[Arkham/Reflect] Class {} at 0x{:X}: {} properties resolved, {} rejected",
             debug_name, class_object, table.size(), rejected);
    return cache.class_properties.emplace(class_object, std::move(table)).first->second;
}

template <typename T>
std::optional<T> ReadClassProperty(Core::Memory::Memory& memory, RuntimeCache& cache,
                                   VAddr object, VAddr class_object, std::string_view class_debug,
                                   const char* name) {
    if (!object) {
        return std::nullopt;
    }
    const auto& table = ResolveClassProperties(memory, cache, class_object, class_debug);
    const auto it = table.find(name);
    if (it == table.end() || !it->second.field_offset) {
        return std::nullopt;
    }
    return Read<T>(memory, object + it->second.field_offset);
}

// Health is spelled differently across the engine and the Batman classes, and the
// dump of BmGame.xxx shows MaxHealth declared as IntProperty on some classes and
// FloatProperty on others. Rather than assuming either, take the first name that
// resolves and convert according to the type recorded on the UProperty itself.
std::optional<s32> ReadScalarAsInt(Core::Memory::Memory& memory, RuntimeCache& cache, VAddr object,
                                   VAddr class_object, std::string_view class_debug,
                                   std::initializer_list<const char*> names) {
    if (!object) {
        return std::nullopt;
    }
    const auto& table = ResolveClassProperties(memory, cache, class_object, class_debug);
    for (const char* name : names) {
        const auto it = table.find(name);
        if (it == table.end() || !it->second.field_offset) {
            continue;
        }
        const VAddr address = object + it->second.field_offset;
        if (it->second.type == "FloatProperty") {
            const auto value = Read<float>(memory, address);
            if (value && std::isfinite(*value)) {
                return static_cast<s32>(*value);
            }
            continue;
        }
        if (it->second.type == "IntProperty") {
            if (const auto value = Read<s32>(memory, address)) {
                return *value;
            }
            continue;
        }
        if (it->second.type == "ByteProperty") {
            if (const auto value = Read<u8>(memory, address)) {
                return static_cast<s32>(*value);
            }
        }
    }
    return std::nullopt;
}

// Most-recently-created live instance of a class, skipping CDOs and archetypes.
// Later entries in GObjects win because a reloaded level re-creates the pawn at a
// higher index while the stale one may not be cleared yet.
VAddr FindInstanceOfClass(Core::Memory::Memory& memory, RuntimeCache& cache, VAddr class_object) {
    if (!class_object) {
        return 0;
    }
    VAddr found{};
    for (const VAddr object : cache.objects) {
        if (!object || !memory.IsValidVirtualAddressRange(object, 0x90)) {
            continue;
        }
        const auto object_class = Read<VAddr>(memory, object + cache.layout.class_offset);
        if (!object_class || *object_class != class_object) {
            continue;
        }
        const std::string name = ObjectName(memory, cache, object);
        if (name.rfind("Default__", 0) == 0 || name.rfind("Archetype", 0) == 0) {
            continue;
        }
        found = object;
    }
    return found;
}

// Live gameplay objects are destroyed and re-created on every level load, so this
// re-runs on a cadence instead of once. Every consumer must tolerate a null result:
// during loading screens, the title menu and cutscene transitions there is no pawn.
void ResolveLiveObjects(Core::Memory::Memory& memory, RuntimeCache& cache) {
    if (!cache.pawn_class) {
        cache.pawn_class = FindClassByName(memory, cache, PlayerPawnClassName);
    }
    if (!cache.controller_class) {
        cache.controller_class = FindClassByName(memory, cache, PlayerControllerClassName);
    }
    if (!cache.inventory_manager_class) {
        cache.inventory_manager_class = FindClassByName(memory, cache, InventoryManagerClassName);
    }
    if (!cache.inventory_gadget_class) {
        cache.inventory_gadget_class = FindClassByName(memory, cache, InventoryGadgetClassName);
    }
    if (!cache.actor_class) {
        cache.actor_class = FindClassByName(memory, cache, "Actor");
    }
    if (!cache.pawn_base_class) {
        cache.pawn_base_class = FindClassByName(memory, cache, "Pawn");
    }

    const VAddr previous_pawn = cache.pawn_object;
    cache.pawn_object = FindInstanceOfClass(memory, cache, cache.pawn_class);
    cache.controller_object = FindInstanceOfClass(memory, cache, cache.controller_class);
    cache.inventory_manager_object =
        FindInstanceOfClass(memory, cache, cache.inventory_manager_class);

    if (cache.pawn_object != previous_pawn) {
        LOG_INFO(Frontend,
                 "[Arkham/Live] Pawn changed 0x{:X} -> 0x{:X} (class 0x{:X}), controller 0x{:X}, "
                 "inventory 0x{:X}",
                 previous_pawn, cache.pawn_object, cache.pawn_class, cache.controller_object,
                 cache.inventory_manager_object);
    }
}

// One-shot instrumentation. The gadget, upgrade and map systems are not yet fully
// traced, and the brief for this mod is explicit that unverified systems get
// diagnostics rather than invented values. This dumps the real shape of the player
// and inventory objects so the next play session tells us the truth instead of us
// guessing offsets or property names.
void LogDiagnosticsOnce(Core::Memory::Memory& memory, RuntimeCache& cache) {
    if (cache.diagnostics_logged || !cache.pawn_object) {
        return;
    }
    cache.diagnostics_logged = true;

    const auto dump = [&](VAddr class_object, std::string_view label) {
        if (!class_object) {
            LOG_WARNING(Frontend, "[Arkham/Diag] {} class not found in GObjects", label);
            return;
        }
        const auto& table = ResolveClassProperties(memory, cache, class_object, label);
        for (const auto& [name, property] : table) {
            LOG_INFO(Frontend, "[Arkham/Diag] {}::{} type={} +0x{:X} dim={} elem={}", label, name,
                     property.type, property.field_offset, property.array_dim,
                     property.element_size);
        }
    };
    dump(cache.pawn_class, PlayerPawnClassName);
    dump(cache.controller_class, PlayerControllerClassName);
    dump(cache.inventory_manager_class, InventoryManagerClassName);
    dump(cache.inventory_gadget_class, InventoryGadgetClassName);
    // Actor and Pawn own Location/Rotation and Health/HealthMax respectively, which is
    // where the live map and health bar actually read from, so confirm their offsets.
    dump(cache.actor_class, "Actor");
    dump(cache.pawn_base_class, "Pawn");

    // ArrayDim is not adjacent to {ElementSize, Offset}, so static arrays such as
    // RPersistentData::UpgradesBought are currently read as scalars. Dump the raw
    // UProperty words around property_base for one known static array and one known
    // scalar: the word that differs between them is the ArrayDim candidate.
    for (const char* probe : {"UpgradesBought", "StoryPercentage"}) {
        const auto it = cache.properties.find(probe);
        if (it == cache.properties.end() || !it->second.object) {
            LOG_WARNING(Frontend, "[Arkham/Diag] ArrayDim probe '{}' not present", probe);
            continue;
        }
        std::array<u32, 16> words{};
        const VAddr window = it->second.object + cache.property_base - 0x20;
        if (!ReadBytes(memory, window, words.data(), sizeof(words))) {
            continue;
        }
        std::string dump_line;
        for (size_t index = 0; index < words.size(); ++index) {
            dump_line += fmt::format("[{:+#x}]={} ", static_cast<int>(index * 4) - 0x20,
                                     words[index]);
        }
        LOG_INFO(Frontend, "[Arkham/Diag] ArrayDim probe '{}' type={} around base: {}", probe,
                 it->second.type, dump_line);
    }

    // Ownership of the individual gadget objects is still unknown. Log every object
    // whose name matches a traced gadget token together with its class and Outer so
    // the relationship can be established from a real session.
    u32 logged{};
    for (const VAddr object : cache.objects) {
        if (!object || logged >= 64 || !memory.IsValidVirtualAddressRange(object, 0x90)) {
            continue;
        }
        const std::string name = ObjectName(memory, cache, object);
        if (name.empty() || GadgetIndex(name) < 0) {
            continue;
        }
        const auto outer = Read<VAddr>(memory, object + cache.layout.outer_offset);
        LOG_INFO(Frontend, "[Arkham/Diag] gadget candidate '{}' class={} at 0x{:X} outer=0x{:X}",
                 name, ObjectClassName(memory, cache, object), object, outer.value_or(0));
        ++logged;
    }
    LOG_INFO(Frontend, "[Arkham/Diag] {} gadget candidate objects logged", logged);
}

void PopulateSnapshot(Core::Memory::Memory& memory, RuntimeCache& cache, SnapshotFrame& frame) {
    frame.gnames_count = static_cast<u32>(std::max(0, cache.names.count));
    frame.gobjects_count = static_cast<u32>(std::max(0, cache.layout.count));
    frame.stage = cache.stage;

    // GObjects grows on every level load, so the object list has to be refreshed
    // before the live pawn can be found again. Doing it on a cadence rather than
    // per poll keeps the cost off the hot path; ~2s at the companion's 250ms poll.
    if (frame.sequence >= cache.live_refresh_sequence) {
        cache.live_refresh_sequence = frame.sequence + 8;
        RefreshObjects(memory, cache);
        ResolveLiveObjects(memory, cache);
        LogDiagnosticsOnce(memory, cache);
    }

    VAddr object = cache.persistent_object;
    if (!object || !memory.IsValidVirtualAddressRange(object, 0x100)) {
        cache.persistent_object = 0;
        RefreshObjects(memory, cache);
        object = cache.persistent_object;
    }
    frame.gobjects_count = static_cast<u32>(std::max(0, cache.layout.count));
    if (!object) {
        frame.status = static_cast<u32>(Status::WaitingForSave);
        return;
    }
    frame.status = static_cast<u32>(Status::Active);
    if (const auto value = ReadProperty<s32>(memory, cache, object, "PlayerHealth")) {
        if (*value >= 0 && *value <= 2000) {
            frame.health = *value;
            frame.availability |= StatsAvailable;
        }
    }
    frame.xp = ReadProperty<float>(memory, cache, object, "XP").value_or(0.0f);
    frame.xp_level = ReadProperty<float>(memory, cache, object, "XPLevel").value_or(0.0f);
    frame.xp_next = ReadProperty<float>(memory, cache, object, "XPForNextUpgrade").value_or(0.0f);
    frame.xp_prev = ReadProperty<float>(memory, cache, object, "XPForPrevUpgrade").value_or(0.0f);
    frame.xp_over = ReadProperty<float>(memory, cache, object, "XPOverUpgradeThreshold").value_or(0.0f);
    if (std::isfinite(frame.xp) && std::isfinite(frame.xp_next) &&
        frame.xp >= 0.0f && frame.xp < 100000000.0f && frame.xp_next >= 0.0f) {
        frame.availability |= XpAvailable;
    } else {
        frame.xp = frame.xp_level = frame.xp_next = frame.xp_prev = frame.xp_over = 0.0f;
    }
    frame.player_level = ReadProperty<s32>(memory, cache, object, "playerLevel").value_or(0);
    frame.story_percentage =
        ReadProperty<s32>(memory, cache, object, "StoryPercentage").value_or(0);
    frame.unlockables =
        ReadProperty<s32>(memory, cache, object, "UnlockablesToSpend").value_or(0);
    frame.total_time = ReadProperty<float>(memory, cache, object, "TotalTimePlayed").value_or(0.0f);
    frame.location_id = ReadProperty<s32>(memory, cache, object, "LocationID").value_or(0);
    frame.save_chapter = ReadProperty<s32>(memory, cache, object, "SaveGameChapter").value_or(0);
    const auto plausible_location = [](const std::array<float, 3>& value) {
        return std::all_of(value.begin(), value.end(), [](float component) {
            return std::isfinite(component) && std::abs(component) < 100000000.0f;
        });
    };

    // Position and facing come from the live pawn wherever possible. The matching
    // fields on RPersistentData are save state and only advance when the game hits a
    // checkpoint, which would leave the companion map frozen while Batman is moving.
    // The persistent values remain a fallback for menus and loading screens, where no
    // pawn exists, so the map shows the last known position instead of jumping to 0,0.
    bool live_position = false;
    if (cache.pawn_object && cache.actor_class) {
        if (const auto location = ReadClassProperty<std::array<float, 3>>(
                memory, cache, cache.pawn_object, cache.actor_class, "Actor", "Location")) {
            if (plausible_location(*location)) {
                frame.player_x = (*location)[0];
                frame.player_y = (*location)[1];
                frame.player_z = (*location)[2];
                frame.availability |= MapAvailable;
                live_position = true;
            }
        }
        if (const auto rotation = ReadClassProperty<std::array<s32, 3>>(
                memory, cache, cache.pawn_object, cache.actor_class, "Actor", "Rotation")) {
            // FRotator yaw is a 16-bit-wrapping signed int; normalise to degrees.
            frame.player_yaw =
                static_cast<float>((*rotation)[1] & 0xFFFF) * (360.0f / 65536.0f);
        }
    }
    if (!live_position) {
        if (const auto location = ReadProperty<std::array<float, 3>>(memory, cache, object,
                                                                     "PlayerLocation")) {
            if (plausible_location(*location)) {
                frame.player_x = (*location)[0];
                frame.player_y = (*location)[1];
                frame.player_z = (*location)[2];
                frame.availability |= MapAvailable;
            }
        }
        if (const auto rotation = ReadProperty<std::array<s32, 3>>(memory, cache, object,
                                                                   "PlayerRotation")) {
            frame.player_yaw =
                static_cast<float>((*rotation)[1] & 0xFFFF) * (360.0f / 65536.0f);
        }
    }

    // Live health from the pawn, so the bar tracks damage in combat rather than the
    // value frozen into the last checkpoint. MaxHealth gives the companion a real
    // denominator; before this it was never written and the UI had no scale.
    // The PlayerHealth read above comes from save state and has already run, so the
    // live value must overwrite it rather than defer to it.
    if (cache.pawn_object) {
        bool live_health = false;
        // Try the Batman pawn class first: if RPawnPlayerBm redeclares either field it
        // shadows the engine one, and the derived value is the authoritative one.
        for (const VAddr owner : {cache.pawn_class, cache.pawn_base_class}) {
            if (!owner) {
                continue;
            }
            const std::string_view label =
                owner == cache.pawn_class ? PlayerPawnClassName : std::string_view{"Pawn"};
            if (!live_health) {
                if (const auto health = ReadScalarAsInt(memory, cache, cache.pawn_object, owner,
                                                        label, {"Health"})) {
                    if (*health >= 0 && *health <= 100000) {
                        frame.health = *health;
                        frame.availability |= StatsAvailable;
                        live_health = true;
                    }
                }
            }
            if (frame.max_health <= 0) {
                if (const auto max_health =
                        ReadScalarAsInt(memory, cache, cache.pawn_object, owner, label,
                                        {"HealthMax", "MaxHealth"})) {
                    if (*max_health > 0 && *max_health <= 100000) {
                        frame.max_health = *max_health;
                    }
                }
            }
        }
    }
    if (frame.max_health > 0) {
        frame.health_percent =
            std::clamp(static_cast<float>(frame.health) / static_cast<float>(frame.max_health),
                       0.0f, 1.0f);
    }
    if (const auto property = cache.properties.find("UpgradesBought");
        property != cache.properties.end() && property->second.field_offset) {
        const u32 count = std::min<u32>(property->second.array_dim, SkillCount);
        bool valid = count > 0;
        u32 bought{};
        for (u32 index = 0; index < count; ++index) {
            const auto value = Read<s32>(memory, object + property->second.field_offset +
                                                    index * property->second.element_size);
            if (!value || *value < 0 || *value > 3) {
                valid = false;
                break;
            }
            frame.skill_states[index] = static_cast<u8>(*value);
            if (*value > 0) ++bought;
        }
        if (valid) {
            frame.upgrades_bought_count = bought;
            frame.availability |= SkillsAvailable;
        }
    }
    if (const auto gadget = ReadProperty<std::array<u32, 2>>(memory, cache, object,
                                                             "CurrentGadgetName")) {
        const std::string name = ReadName(memory, cache, (*gadget)[0]);
        frame.current_gadget = GadgetIndex(name);
        if (frame.current_gadget >= 0) {
            frame.gadget_mask |= 1U << frame.current_gadget;
            frame.availability |= GadgetsAvailable;
        }
    } else {
        frame.current_gadget = -1;
    }
    const auto objective_property = cache.properties.find("PrimaryObjective");
    const auto description_property = cache.properties.find("Description");
    std::string objective;
    std::string description;
    if (objective_property != cache.properties.end() && objective_property->second.field_offset) {
        objective = ReadFString(memory, object + objective_property->second.field_offset);
    }
    if (description_property != cache.properties.end() && description_property->second.field_offset) {
        description = ReadFString(memory, object + description_property->second.field_offset);
    }
    if (!objective.empty() || !description.empty()) {
        CopyText(frame.objective, sizeof(frame.objective), objective);
        CopyText(frame.description, sizeof(frame.description), description);
        frame.availability |= ObjectiveAvailable;
    }
    // No location-name table has been traced out of the game data yet. LocationID is
    // real and is forwarded as a number; inventing a display name here ("Location 5")
    // would look like resolved data on the companion screen when nothing was resolved.
    // The name stays empty until the real mapping is recovered from the game's
    // localisation packages.
    frame.location[0] = '\0';
}

void ResetCache(RuntimeCache& cache, VAddr main_base, u64 process_id) {
    cache.main_base = main_base;
    cache.process_id = process_id;
    cache.stage = 0;
    cache.failed = false;
    cache.names = {};
    cache.layout = {};
    cache.objects.clear();
    cache.object_set.clear();
    cache.persistent_class = 0;
    cache.persistent_object = 0;
    cache.persistent_default = 0;
    cache.property_base = 0;
    cache.property_value_member = 0;
    cache.next_retry_sequence = 0;
    cache.properties.clear();
    cache.pawn_class = 0;
    cache.pawn_object = 0;
    cache.controller_class = 0;
    cache.controller_object = 0;
    cache.inventory_manager_class = 0;
    cache.inventory_manager_object = 0;
    cache.inventory_gadget_class = 0;
    cache.actor_class = 0;
    cache.pawn_base_class = 0;
    cache.class_properties.clear();
    cache.live_refresh_sequence = 0;
    cache.diagnostics_logged = false;
}

}

int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size) {
    if (!output || output_size < sizeof(SnapshotFrame)) {
        return -static_cast<int>(sizeof(SnapshotFrame));
    }
    SnapshotFrame frame{};
    frame.magic = Magic;
    frame.version = 1;
    frame.size = sizeof(SnapshotFrame);
    frame.sequence = Sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    frame.status = static_cast<u32>(Status::NotRunning);
    frame.current_gadget = -1;
    if (!session.IsRunning()) {
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != TitleId) {
        frame.status = static_cast<u32>(Status::WrongTitle);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    if (!main_base || !main_size) {
        frame.status = static_cast<u32>(Status::Resolving);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    if (!IsExpectedBuild(system)) {
        frame.status = static_cast<u32>(Status::UnsupportedBuild);
        std::memcpy(output, &frame, sizeof(frame));
        return sizeof(frame);
    }
    auto& memory = system.ApplicationMemory();
    const auto* process = system.ApplicationProcess();
    const u64 process_id = process ? process->GetProcessId() : 0;
    std::scoped_lock lock{Cache.mutex};
    if (Cache.main_base != main_base || Cache.process_id != process_id) {
        ResetCache(Cache, main_base, process_id);
    }
    if (Cache.stage < 6 && frame.sequence >= Cache.next_retry_sequence) {
        if (!ResolveRuntime(memory, main_base, main_size, Cache)) {
            Cache.next_retry_sequence = frame.sequence + (Cache.stage == 5 ? 40 : 8);
            LOG_WARNING(Frontend, "[Arkham Companion] Reflection discovery stopped at stage {}",
                        Cache.stage);
        }
    }
    frame.stage = Cache.stage;
    frame.gnames_count = static_cast<u32>(std::max(0, Cache.names.count));
    frame.gobjects_count = static_cast<u32>(std::max(0, Cache.layout.count));
    if (Cache.stage < 6) {
        frame.status = static_cast<u32>(Status::Resolving);
    } else {
        PopulateSnapshot(memory, Cache, frame);
    }
    std::memcpy(output, &frame, sizeof(frame));
    return sizeof(frame);
}

}

#pragma once
#include "NativeRead.h"
#include <array>

namespace ac2ds::native {
// These readers accept a native LogicalInventory selected by a verified owner.
// They deliberately do not search memory for vtables or guess a player owner.
// Layout evidence: 010d2c70, 010d2d10, 010de9c0, 012d80d0, 010de600,
// 010db600, 012dc150 and constructors 010d3f30 / 010e0160 / 012d84a0.
struct PointerArray {
    uint32_t data{};
    uint32_t flags{};
    uint32_t Count() const { return (flags >> 16) & 0x3fff; }
    bool operator==(const PointerArray& other) const { return data == other.data && flags == other.flags; }
};
inline bool ArrayHeader(const Memory& memory, uint32_t owner, uint32_t offset,
        uint32_t limit, PointerArray& result) {
    return owner >= 0x10000 && owner <= UINT32_MAX - offset && memory.Read(owner + offset, result) &&
        result.Count() <= limit && (!result.Count() ||
            (result.data >= 0x10000 && result.data <= UINT32_MAX - result.Count() * 4));
}
inline bool SameArray(const Memory& memory, uint32_t owner, uint32_t offset, const PointerArray& original) {
    PointerArray after{};
    return ArrayHeader(memory, owner, offset, 4096, after) && after == original;
}
struct InventoryContainer {
    uint32_t identity{}, category{}, level{}, quantity{}, capacity{};
    bool rechargeable{};
};
struct InventoryContents {
    uint32_t identity{}, settingsIdentity{};
    std::vector<InventoryContainer> containers;
};

struct PlayerInventoryOwner {
    uint32_t player{}, component{}, entity{}, context{}, dataItem{};
    PointerArray components{}, dataItems{};
    uint16_t componentCache{};
    uint8_t componentEpoch{};
    bool operator==(const PlayerInventoryOwner& other) const {
        return player == other.player && component == other.component && entity == other.entity &&
            context == other.context && dataItem == other.dataItem && components == other.components &&
            dataItems == other.dataItems && componentCache == other.componentCache && componentEpoch == other.componentEpoch;
    }
};

// 0054eec0 -> 00948dc0 resolves the already-cached player script component.
// Inventory subscriber 015f75c0 -> 010baa60 -> 010ba720 selects its parent's
// first InventoryDataItem. The loaded-player probe verified that parent is
// the same entity reached through this component; no event/memory scan is needed.
// We deliberately reject unresolved native caches instead of refreshing them.
inline std::optional<PlayerInventoryOwner> ResolvePlayerInventory(const Memory& memory, uint32_t player) {
    PlayerInventoryOwner owner; owner.player = player;
    uint32_t componentType{}, componentPlayer{}, dataType{}, interfaceType{}, logicalType{};
    if (!memory.Member(player, 0xd5, owner.componentCache) ||
        !memory.Member(player, 0xf4, owner.componentEpoch) ||
        uint8_t(owner.componentCache) != owner.componentEpoch ||
        !ArrayHeader(memory, player, 0x74, 256, owner.components)) return {};
    uint32_t index = owner.componentCache >> 8;
    if (index >= 0xfe || index >= owner.components.Count() ||
        !memory.Field(owner.components.data, index * 4, owner.component) ||
        !memory.Field(owner.component, 0, componentType) || componentType != 0x01e9cd48 ||
        !memory.Field(owner.component, 8, componentPlayer) || componentPlayer != player ||
        !memory.Field(owner.component, 0x10, owner.entity) ||
        !memory.Field(owner.entity, 0x20, owner.context) ||
        !ArrayHeader(memory, owner.context, 0x58, 256, owner.dataItems) || !owner.dataItems.Count() ||
        !memory.Field(owner.dataItems.data, 0xc, owner.dataItem) ||
        !memory.Field(owner.dataItem, 0, dataType) || dataType != 0x01e797c8 ||
        !memory.Field(owner.dataItem, 4, interfaceType) || interfaceType != 0x01e797c0 ||
        !memory.Field(owner.dataItem, 0xc, logicalType) || logicalType != 0x01e57e40) return {};
    return owner;
}

inline std::optional<uint32_t> ReadInventoryCapacity(const Memory& memory, uint32_t settings,
        uint32_t category, uint32_t level) {
    // 010de600 selects settings+4c. 010db600 matches category at +8;
    // 012dc150 matches the current level at +8 and reads its capacity at +c.
    PointerArray categories{};
    if (!ArrayHeader(memory, settings, 0x50, 128, categories)) return {};
    std::optional<uint32_t> capacity;
    for (uint32_t i = 0; i < categories.Count(); ++i) {
        uint32_t entry{}, entryCategory{};
        if (!memory.Field(categories.data, i * 4, entry)) return {};
        if (!entry) continue; // The native category lookup skips empty entries.
        if (!memory.Field(entry, 8, entryCategory)) return {};
        if (entryCategory != category) continue;
        PointerArray levels{};
        if (!ArrayHeader(memory, entry, 0x10, 128, levels)) return {};
        for (uint32_t j = 0; j < levels.Count(); ++j) {
            uint32_t row{}, rowLevel{}, rowCapacity{};
            if (!memory.Field(levels.data, j * 4, row) || !memory.Field(row, 8, rowLevel)) return {};
            if (rowLevel != level) continue;
            if (!memory.Field(row, 0xc, rowCapacity)) return {};
            capacity = rowCapacity;
            break;
        }
        if (!SameArray(memory, entry, 0x10, levels)) return {};
        break;
    }
    if (!SameArray(memory, settings, 0x50, categories)) return {};
    // Although the engine returns zero for a missing settings entry, absent
    // metadata is unknown here, not evidence of a zero-capacity possession.
    return capacity;
}

inline std::optional<InventoryContents> ReadInventory(const Memory& memory, uint32_t logicalInventory) {
    uint32_t vtable{}, settings{};
    if (!memory.Field(logicalInventory, 0, vtable) || vtable != 0x01e57e40 ||
        !memory.Read(0x0223f4f4, settings) || !settings) return {};
    PointerArray array{};
    if (!ArrayHeader(memory, logicalInventory, 4, 128, array)) return {};
    InventoryContents result; result.identity = logicalInventory; result.settingsIdentity = settings;
    for (uint32_t i = 0; i < array.Count(); ++i) {
        uint32_t address{};
        if (!memory.Field(array.data, i * 4, address)) return {};
        if (!address) continue;
        std::array<uint32_t, 12> before{};
        if (!memory.Read(address, before)) return {};
        // Unknown subclasses can override quantity semantics; never apply a
        // guessed common layout to them.
        if (before[0] != 0x01e58098 && before[0] != 0x01e790d8) return {};
        InventoryContainer item;
        item.identity = address; item.category = before[2]; item.level = before[3];
        item.rechargeable = before[0] == 0x01e790d8;
        item.quantity = item.rechargeable ? before[4] : (before[7] >> 16) & 0x3fff;
        auto capacity = ReadInventoryCapacity(memory, settings, item.category, item.level);
        if (!capacity || item.quantity > *capacity) return {};
        item.capacity = *capacity;
        for (const auto& prior : result.containers) if (prior.category == item.category) return {};
        std::array<uint32_t, 12> after{};
        if (!memory.Read(address, after) || before != after) return {};
        result.containers.push_back(item);
    }
    uint32_t afterVtable{}, afterSettings{};
    if (!SameArray(memory, logicalInventory, 4, array) ||
        !memory.Field(logicalInventory, 0, afterVtable) || afterVtable != vtable ||
        !memory.Read(0x0223f4f4, afterSettings) || afterSettings != settings) return {};
    return result;
}

inline std::optional<InventoryContents> ReadPlayerInventory(const Memory& memory, uint32_t player) {
    auto owner = ResolvePlayerInventory(memory, player);
    if (!owner) return {};
    // ResolvePlayerInventory already validated dataItem+c without wrapping.
    auto contents = ReadInventory(memory, owner->dataItem + 0xc);
    auto after = ResolvePlayerInventory(memory, player);
    if (!contents || !after || !(*owner == *after)) return {};
    return contents;
}
}

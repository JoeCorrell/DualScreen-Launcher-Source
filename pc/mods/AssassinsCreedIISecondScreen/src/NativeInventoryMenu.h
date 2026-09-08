#pragma once
#include "NativeInventory.h"
#include "NativeArtwork.h"

namespace ac2ds::native {
struct ReloadDefinition {
    uint32_t menuId{}, category{}, identity{}, titleId{}, descriptionId{};
    bool locked{};
    uint32_t artworkId{}, thumbnailId{};
};
// Original EquipmentInfo.as + 012d8aa0's exact jump table. Menu IDs are
// presentation categories, not instance IDs or commands.
inline uint32_t ReloadCategory(uint32_t menuId) {
    switch (menuId) {
        case 10: return 5;  // Smoke bombs.
        case 11: return 8;  // Throwing knives (individual native entries).
        case 12: return 10; // Gun bullets.
        case 13: return 11; // Poison.
        case 14: return 7;  // Medicine.
        default: return 0;
    }
}

// Mirrors the read-only 010f7c00 -> 0115a210(3) -> 01158dc0(menuId)
// definition selection and 012d96c0's actual byte +30 lock test.
// No menu context is constructed, no cache is refreshed and no native call is made.
inline std::optional<std::vector<ReloadDefinition>> ReadReloadDefinitions(const Memory& memory) {
    uint32_t manager{}, catalog{}, reloadGroup{}, catalogSlot{}, groupSlot{}, groupReference{};
    PointerArray catalogs{}, groups{}, entries{};
    if (!memory.Read(0x0223f81c, manager) || !ArrayHeader(memory, manager, 0xc, 64, catalogs)) return {};
    for (uint32_t i = 0; i < catalogs.Count(); ++i) {
        uint32_t candidate{}, type{};
        if (!memory.Field(catalogs.data, i * 4, candidate)) return {};
        if (!candidate) continue;
        if (!memory.Field(candidate, 0, type)) return {};
        // This exact class's +10 type getter returns 02133c74, the same
        // descriptor sought by 010f7c00. Untraced subclasses are rejected.
        if (type == 0x01e610c8) {
            if (catalog) return {}; catalog = candidate; catalogSlot = catalogs.data + i * 4;
        }
    }
    if (!catalog || !ArrayHeader(memory, catalog, 8, 32, groups)) return {};
    for (uint32_t i = 0; i < groups.Count(); ++i) {
        uint32_t reference{}, group{}, id{};
        if (!memory.Field(groups.data, i * 4, reference) || !memory.Reference(reference, group)) return {};
        if (!group) continue;
        if (!memory.Field(group, 0x10, id)) return {};
        if (id == 3) {
            if (reloadGroup) return {}; reloadGroup = group;
            groupSlot = groups.data + i * 4; groupReference = reference;
        }
    }
    if (!reloadGroup || !ArrayHeader(memory, reloadGroup, 8, 32, entries)) return {};
    std::vector<ReloadDefinition> result;
    for (uint32_t i = 0; i < entries.Count(); ++i) {
        uint32_t reference{}, entry{}, definition{};
        if (!memory.Field(entries.data, i * 4, reference) || !memory.Reference(reference, entry)) return {};
        if (!entry) continue;
        std::array<uint32_t, 12> before{};
        if (!memory.Read(entry, before) || before[0] != 0x01e61020) return {};
        uint32_t category = ReloadCategory(before[5]);
        if (!category) return {};
        if (!memory.Reference(before[2], definition)) return {};
        if (!definition) continue; // Unresolved definitions never imply unlocked.
        uint32_t type{}, nativeCategory{}; uint8_t locked{};
        if (!memory.Field(definition, 0, type) || type != 0x01e699bc ||
            !memory.Field(definition, 0xc, nativeCategory) || nativeCategory != category ||
            !memory.Member(definition, 0x30, locked)) return {};
        for (const auto& prior : result) if (prior.menuId == before[5]) return {};
        result.push_back({before[5], category, definition, before[6], before[9], locked != 0});
        if (!locked && (!ReadArtworkId(memory, before[3], result.back().artworkId) ||
            !ReadArtworkId(memory, before[4], result.back().thumbnailId))) return {};
        std::array<uint32_t, 12> after{}; uint32_t afterDefinition{}, afterEntry{}, afterReference{}; uint8_t afterLock{};
        if (!memory.Read(entry, after) || before != after ||
            !memory.Field(entries.data, i * 4, afterReference) || afterReference != reference ||
            !memory.Reference(reference, afterEntry) || afterEntry != entry ||
            !memory.Reference(before[2], afterDefinition) || afterDefinition != definition ||
            !memory.Member(definition, 0x30, afterLock) || locked != afterLock) return {};
    }
    uint32_t afterManager{}, afterType{}, afterCatalog{}, afterGroup{}, afterReference{}, afterGroupId{};
    if (!memory.Read(0x0223f81c, afterManager) || afterManager != manager ||
        !memory.Field(catalog, 0, afterType) || afterType != 0x01e610c8 ||
        !memory.Read(catalogSlot, afterCatalog) || afterCatalog != catalog ||
        !memory.Read(groupSlot, afterReference) || afterReference != groupReference ||
        !memory.Reference(groupReference, afterGroup) || afterGroup != reloadGroup ||
        !memory.Field(reloadGroup, 0x10, afterGroupId) || afterGroupId != 3 ||
        !SameArray(memory, manager, 0xc, catalogs) || !SameArray(memory, catalog, 8, groups) ||
        !SameArray(memory, reloadGroup, 8, entries)) return {};
    return result;
}
}

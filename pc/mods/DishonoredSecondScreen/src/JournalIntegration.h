#pragma once
#include "Bridge.h"

namespace dishonored {
inline constexpr uint64_t KeyRingKeyPrefix = uint64_t(0x401) << 32;

// Equipment has already read the native key ring on this game-thread sample.
// Share its arena references, not another scan or an independently kept list.
// Abstract inventory keys already pass through Journal's document grouping;
// only the separate DisKeyRing records belong in this merge.
inline bool AppendKeyRingJournal(bridge::Snapshot& snapshot) noexcept {
    auto& journal = snapshot.journal;
    const auto& equipment = snapshot.equipment;
    if (equipment.domain.stored > equipment.entries.size() ||
        journal.domain.stored > journal.entries.size()) return false;
    bool complete = true;
    for (uint32_t i = 0; i < equipment.domain.stored; ++i) {
        const auto& key = equipment.entries[i];
        if (key.kind != bridge::EquipmentKind::Key ||
            (key.key & 0xffffffff00000000ull) != KeyRingKeyPrefix) continue;
        bool present = false;
        for (uint32_t j = 0; j < journal.domain.stored; ++j)
            if (journal.entries[j].key == key.key) { present = true; break; }
        if (present) continue;
        ++journal.domain.total;
        if (journal.domain.stored == journal.entries.size() ||
            !bridge::ValidText(snapshot, key.label.title) ||
            !bridge::ValidText(snapshot, key.label.description)) { complete = false; continue; }
        auto& entry = journal.entries[journal.domain.stored++];
        entry = {};
        entry.key = key.key;
        entry.label = key.label;
        entry.kind = bridge::JournalKind::Key;
        entry.contents = key.label.description;
        // An optional null DisTweaks_Key has no description in the game.
        // Known empty text is complete, and must not invoke the item getter.
        entry.contentsComplete = true;
    }
    return complete;
}
}

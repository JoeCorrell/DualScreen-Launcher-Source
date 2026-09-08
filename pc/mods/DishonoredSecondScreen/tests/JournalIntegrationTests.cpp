#include "../src/JournalIntegration.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace dishonored;
int main() {
    auto storage = std::make_unique<bridge::Snapshot>();
    auto& s = *storage;
    s.text[1] = u'K'; s.text[2] = 0; s.text[3] = u'D'; s.text[4] = 0; s.textUsed = 5;
    auto& key = s.equipment.entries[0];
    key.key = KeyRingKeyPrefix | 12; key.kind = bridge::EquipmentKind::Key;
    key.label.title = {1, 1}; key.label.description = {3, 1}; key.label.artwork.id = 101;
    // A key from the separate abstract inventory already has its own document
    // path. Do not duplicate it based only on EquipmentKind::Key.
    s.equipment.entries[1] = key; s.equipment.entries[1].key = 999;
    s.equipment.domain.stored = 2;
    assert(AppendKeyRingJournal(s));
    assert(s.journal.domain.stored == 1 && s.journal.domain.total == 1);
    const auto& entry = s.journal.entries[0];
    assert(entry.key == key.key && entry.kind == bridge::JournalKind::Key);
    assert(entry.contentsComplete && entry.contents.offset == 3 && entry.label.artwork.id == 101);
    assert(s.textUsed == 5); // No duplicate arena allocation.
    assert(AppendKeyRingJournal(s) && s.journal.domain.stored == 1 && s.journal.domain.total == 1);
    s.journal = {}; key.label.description = {};
    assert(AppendKeyRingJournal(s) && s.journal.entries[0].contentsComplete);
    assert(!s.journal.entries[0].contents.length); // Native key with no tweak.
    s.journal = {}; key.label.title = {4, 10};
    assert(!AppendKeyRingJournal(s) && s.journal.domain.stored == 0 && s.journal.domain.total == 1);
    key.label.title = {1, 1}; s.journal = {};
    s.journal.domain.stored = s.journal.entries.size(); s.journal.domain.total = s.journal.domain.stored;
    assert(!AppendKeyRingJournal(s) && s.journal.domain.total == s.journal.entries.size() + 1);
    s.journal = {}; s.equipment = {}; // Save replacement removes old keys.
    assert(AppendKeyRingJournal(s) && !s.journal.domain.stored);
    static_assert(bridge::StatisticCapacity >= 19 + 9 * 32);
    puts("PASS: native key-ring journal merge, ownership source, descriptions, deduplication, bounds and lifecycle reset");
}

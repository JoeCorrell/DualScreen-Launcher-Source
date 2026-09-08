#include "StateAdapter.h"
#include <algorithm>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace dishonored {
namespace {
using ReadableGetter = GameArray* (__attribute__((fastcall)) *)(void*, void*, GameArray*);
using StringDestructor = void (__attribute__((fastcall)) *)(GameArray*, void*);
using PlayAudio = void (__attribute__((fastcall)) *)(void*, void*, int32_t, void*);
using StopAudio = void (__attribute__((fastcall)) *)(void*, void*, int32_t, int32_t);
bool StoreAudioName(Address address, GameName name) noexcept {
    SIZE_T written{};
    return Memory::Span(address, sizeof(name)) && WriteProcessMemory(GetCurrentProcess(),
        reinterpret_cast<void*>(uintptr_t(address)), &name, sizeof(name), &written) && written == sizeof(name);
}
}

uint64_t StateAdapter::JournalGroupSignature(uint32_t entry) noexcept {
    uint64_t hash = 14695981039346656037ull;
    auto add = [&](int32_t index) {
        const auto& record = journal_->records[uint32_t(index)];
        const uint32_t words[] = {record.item, uint32_t(record.quantity), record.flags};
        for (uint32_t word : words) { hash ^= word; hash *= 1099511628211ull; }
    };
    add(journal_->roots[entry]);
    for (int32_t child = journal_->heads[entry]; child >= 0; child = journal_->next[uint32_t(child)]) add(child);
    return hash;
}

bool StateAdapter::AudioLogContext(Address* actorOut, Address* componentOut) noexcept {
    Address actor{}, field{}, table{}, component{}, getter{}; uint32_t flags{};
    if (actorOut) *actorOut = 0;
    if (componentOut) *componentOut = 0;
    // Pawn+4cc speaker interface forwards to the audio-log actor at +9dc.
    // Its +248 interface getter +64 -> 00848560 returns receiver+8.
    // Native text lookup and hook parameters both require component+4c bit0.
    const bool valid = FieldAt(pawn_, u"m_pAudioLogConvActor", u"ObjectProperty", 4, field) && field == pawn_ + 0x9dc &&
        memory_.Read(field, actor) && memory_.IsA(actor, u"DisPlayerAudioLogDummyActor") && memory_.LiveActor(actor) &&
        memory_.Read(actor, table) && table == memory_.Image() + 0xcee498 &&
        memory_.Read(actor + 0x248, table) && table == memory_.Image() + 0xcee420 &&
        memory_.Read(table + 0x60, getter) && getter == memory_.Image() + 0x88b5e0 &&
        memory_.Read(table + 0x64, getter) && getter == memory_.Image() + 0x448560 &&
        memory_.Read(pawn_ + 0x4cc, table) && table == memory_.Image() + 0xd0f620 &&
        memory_.Read(table + 0x60, getter) && getter == memory_.Image() + 0x6a2030 &&
        FieldAt(actor, u"m_pConvComponent", u"ComponentProperty", 4, field) && field == actor + 0x250 &&
        memory_.Read(field, component) && memory_.IsA(component, u"DisConversationComponent") &&
        memory_.Read(component + 0x4c, flags) && (flags & 1) &&
        FieldAt(component, u"m_HookParams", u"StructProperty", 16, field) && field == component + 0x124;
    if (valid) {
        if (actorOut) *actorOut = actor;
        if (componentOut) *componentOut = component;
    }
    return valid;
}

bridge::Value<bool> StateAdapter::AudioLogPlaying(Address item) noexcept {
    Address component{}, field{}, running{}, dialog{}; GameName name{}, current{}; uint8_t state{};
    if (!memory_.IsA(item, u"DisAbstractItemAudioLog") ||
        !FieldAt(item, u"m_AudioLogName", u"NameProperty", 8, field) || field != item + 0x74 ||
        !memory_.Read(field, name) || !AudioLogContext(nullptr, &component)) return {};
    if (!(name.index || name.number)) return {false, true};
    // BC1860 matches the native hook's audio-log FName, then speaker +30.
    // C9CE30 returns running-instance+14 only while instance+C is nonnull.
    // This reads actual conversation state, not the journal's requested name.
    if (!memory_.Read(component + 0x12c, current)) return {};
    if (name.index != current.index || name.number != current.number) return {false, true};
    if (!FieldAt(component, u"m_pDialogRunningInst", u"StructProperty", 4, field) || field != component + 0x8c ||
        !memory_.Read(field, running)) return {};
    if (!running) return {false, true};
    if (!memory_.Read(running + 0xc, dialog)) return {};
    if (!dialog) return {false, true};
    if (!memory_.Read(running + 0x14, state)) return {};
    return {state != 0, true};
}

void StateAdapter::AudioLogState(Address item, bridge::JournalEntry& entry) noexcept {
    entry.playing = AudioLogPlaying(item);
    if (!entry.playing.known) return;
    Address field{}; GameName name{};
    if (!FieldAt(item, u"m_AudioLogName", u"NameProperty", 8, field) || !memory_.Read(field, name)) return;
    const auto eligible = NativeInput(0x10);
    entry.canPlay = {nativeActions_ && eligible.value && (name.index || name.number) && !entry.playing.value, eligible.known};
    entry.canStop = {nativeActions_ && eligible.value && entry.playing.value, eligible.known};
}

bridge::CommandResult StateAdapter::AudioLogAction(const bridge::Command& command, const bridge::Snapshot& snapshot) noexcept {
    using Result = bridge::CommandResult;
    const bool stop = command.kind == bridge::CommandKind::StopAudiograph;
    if (!journal_->valid || snapshot.journal.domain.stored > snapshot.journal.entries.size()) return Result::RejectedUnavailable;
    uint32_t index{};
    for (; index < snapshot.journal.domain.stored; ++index) if (snapshot.journal.entries[index].key == command.key) break;
    if (index == snapshot.journal.domain.stored || index >= journal_->entryCount ||
        snapshot.journal.entries[index].kind != bridge::JournalKind::Audiograph) return Result::RejectedOwnership;
    const int32_t root = journal_->roots[index];
    Address inventory{}, owner{}, field{}; GameArray items{}; JournalWorkspace::Record record{}; GameName name{};
    if (!memory_.Read(pawn_ + 0x59c, inventory) || !memory_.IsA(inventory, u"DishonoredInventory") ||
        !memory_.Array(inventory + 0xc8, items, journal_->records.size(), sizeof(record)) || root < 0 || root >= items.count ||
        !memory_.Read(items.data + uint32_t(root) * sizeof(record), record)) return Result::FailedValidation;
    // Revalidate this owned row at execution time; no movie list index is used.
    if (std::memcmp(&record, &journal_->records[uint32_t(root)], sizeof(record)) || (record.flags & 1) ||
        !memory_.IsA(record.item, u"DisAbstractItemAudioLog") || !memory_.Read(record.item + 0x70, owner) || owner ||
        Key(record.item, bridge::Page::Journal) != command.key) return Result::RejectedOwnership;
    if (!FieldAt(record.item, u"m_AudioLogName", u"NameProperty", 8, field) || field != record.item + 0x74 ||
        !memory_.Read(field, name) || !(name.index || name.number)) return Result::RejectedUnavailable;
    const auto eligible = NativeInput(0x10);
    if (!eligible.known) return Result::FailedValidation;
    if (!eligible.value) return Result::RejectedEligibility;
    const auto playing = AudioLogPlaying(record.item);
    if (!playing.known) return Result::FailedValidation;
    if (playing.value != stop) return Result::Applied;
    Address actor{}, component{}, table{}, play{}, stopFunction{}, hookGetter{}, componentGetter{}, actorPlay{};
    if (!AudioLogContext(&actor, &component) || !memory_.Read(pawn_ + 0x4cc, table) || table != memory_.Image() + 0xd0f620 ||
        !memory_.Read(table + 0x28, play) || play != memory_.Image() + 0x6a1ff0 ||
        !memory_.Read(table + 0x60, hookGetter) || hookGetter != memory_.Image() + 0x6a2030 ||
        !memory_.Read(actor + 0x248, table) || !memory_.Read(table + 0x34, stopFunction) || stopFunction != memory_.Image() + 0x8a4b70 ||
        !memory_.Read(table + 0x28, actorPlay) || actorPlay != memory_.Image() + 0x8a6e00 ||
        !memory_.Read(table + 0x64, componentGetter) || componentGetter != memory_.Image() + 0x448560) return Result::FailedValidation;
    if (stop) {
        // BC1A30 stops only a matching playing log, with native reasons 9/D.
        reinterpret_cast<StopAudio>(uintptr_t(stopFunction))(reinterpret_cast<void*>(uintptr_t(actor + 0x248)), nullptr, 9, 0xd);
    } else {
        // BC1950 writes the log FName to the existing speaker hook parameters
        // then fires hook 76. AA1FF0 handles native replacement and playback.
        // Never write the journal movie's selection/request fields.
        if (!StoreAudioName(component + 0x12c, name)) return Result::FailedValidation;
        void* speaker = reinterpret_cast<void*>(uintptr_t(pawn_ + 0x4cc));
        reinterpret_cast<PlayAudio>(uintptr_t(play))(speaker, nullptr, 0x76, speaker);
    }
    return Result::Applied;
}

bool StateAdapter::CopyNativeDocumentPart(Address item, uint32_t& length) noexcept {
    Address table{}, getter{}, field{};
    if (!memory_.IsA(item, u"DisAbstractItem") || !memory_.Read(item, table) ||
        !memory_.Read(table + 0x124, getter)) return false;
    const bool audio = getter == memory_.Image() + 0x651500 && memory_.IsA(item, u"DisAbstractItemAudioLog");
    if (!audio && getter != memory_.Image() + 0x64dc00) return false;
    if (audio && !AudioLogContext()) return false;
    uint32_t partLength{};
    if (audio) {
        // Exact native journal getter, invoked once per explicit read request.
        // 00a51500 -> 00aaccf0 -> 00ca4ad0/00ca4650 -> 00c9ee20 walks
        // conversation text/condition virtuals. This is the transcript path;
        // playback uses the distinct speaker +28 fire-hook operation.
        Address actor{}, component{}, actorType{}, componentType{}; GameName previousName{}, requestedName{};
        int32_t actorIndex{}, componentIndex{};
        if (!AudioLogContext(&actor, &component) || !memory_.Object(actor, actorType) || !memory_.Object(component, componentType) ||
            !memory_.Read(actor + 4, actorIndex) || !memory_.Read(component + 4, componentIndex) ||
            !memory_.Read(component + 0x12c, previousName) || !memory_.Read(item + 0x74, requestedName)) return false;
        GameArray result{};
        reinterpret_cast<ReadableGetter>(uintptr_t(getter))(reinterpret_cast<void*>(uintptr_t(item)), nullptr, &result);
        const bool copied = memory_.String(Address(uintptr_t(&result)), stringBuffer_.get(), 65536, partLength);
        // This FString was allocated by the game. Release through the exact
        // native destructor, never a payload heap allocator or a leaked copy.
        reinterpret_cast<StringDestructor>(uintptr_t(memory_.Image() + 0x30ee0))(&result, nullptr);
        if (requestedName.index || requestedName.number) {
            // A51500 temporarily selects its transcript in shared hook scratch
            // parameters. Restore only that FName, on the same live component,
            // so reading another transcript cannot relabel a playing recording.
            // A large/failed FString read must not exhaust the budget needed
            // for cleanup. This independent reader was allocated on bootstrap,
            // not as a large temporary on the game's stack.
            auto& cleanup = *cleanupMemory_; cleanup.Begin(4096, 256);
            Address currentPawn{}, currentActor{}, currentComponent{}, type{}; uint32_t flags{}; GameName current{};
            if (!cleanup.ObjectsQuiescent() || !cleanup.Read(memory_.Image() + 0x1052de8, currentPawn) || currentPawn != pawn_ ||
                !cleanup.LiveActor(currentPawn) || !cleanup.Read(currentPawn + 0x9dc, currentActor) || currentActor != actor ||
                !cleanup.Registered(actor, actorIndex) || !cleanup.Object(actor, type) || type != actorType || !cleanup.LiveActor(actor) ||
                !cleanup.Read(actor + 0x250, currentComponent) || currentComponent != component ||
                !cleanup.Registered(component, componentIndex) || !cleanup.Object(component, type) || type != componentType ||
                !cleanup.Read(component + 0x4c, flags) || !(flags & 1) || !cleanup.Read(component + 0x12c, current) ||
                current.index != requestedName.index || current.number != requestedName.number ||
                !StoreAudioName(component + 0x12c, previousName)) return false;
        }
        if (!copied) return false;
    } else if (!FieldAt(item, u"m_Description", u"StrProperty", 12, field) || field != item + 0x50 ||
        !memory_.String(field, stringBuffer_.get(), 65536, partLength)) return false;
    if (!partLength) return true;
    const uint32_t separator = length ? 2 : 0;
    if (length >= document_->text.size() || partLength + separator + 1 > document_->text.size() - length) return false;
    if (separator) { document_->text[length++] = u'\n'; document_->text[length++] = u'\n'; }
    std::copy_n(stringBuffer_.get(), partLength, document_->text.data() + length);
    length += partLength; document_->text[length] = 0;
    return true;
}

bridge::CommandResult StateAdapter::ReadDocument(bridge::Key key, const bridge::Snapshot& snapshot) noexcept {
    using Result = bridge::CommandResult;
    uint32_t entryIndex = snapshot.journal.domain.stored;
    if (entryIndex > snapshot.journal.entries.size()) return Result::FailedValidation;
    for (uint32_t i = 0; i < snapshot.journal.domain.stored; ++i)
        if (snapshot.journal.entries[i].key == key) { entryIndex = i; break; }
    if (entryIndex == snapshot.journal.domain.stored) return Result::RejectedOwnership;
    if (!journal_->valid || entryIndex >= journal_->entryCount) return Result::RejectedUnavailable;
    const int32_t root = journal_->roots[entryIndex];
    if (root < 0 || uint32_t(root) >= journal_->records.size()) return Result::FailedValidation;
    memory_.Begin(1024 * 1024, 16384);
    Address pawn{}, inventory{}; GameArray items{};
    if (!Roots(controller_, pawn) || pawn != pawn_ || !memory_.Read(pawn + 0x59c, inventory) ||
        !memory_.IsA(inventory, u"DishonoredInventory") ||
        !memory_.Array(inventory + 0xc8, items, journal_->records.size(), sizeof(JournalWorkspace::Record)) ||
        root >= items.count) return Result::FailedValidation;
    if (items.count != journal_->recordCount) return Result::RejectedStale;
    // Validate every record and parent against the workspace that produced
    // this snapshot. Only owned native children may contribute readable pages.
    std::array<JournalWorkspace::Record, 32> batch{};
    for (uint32_t i = 0; i < uint32_t(items.count);) {
        const uint32_t count = std::min<uint32_t>(batch.size(), uint32_t(items.count) - i);
        if (!memory_.Bytes(items.data + i * sizeof(batch[0]), batch.data(), count * sizeof(batch[0])) ||
            std::memcmp(batch.data(), journal_->records.data() + i, count * sizeof(batch[0]))) return Result::RejectedStale;
        for (uint32_t j = 0; j < count; ++j) {
            Address parent{};
            if (!memory_.IsA(batch[j].item, u"DisAbstractItem") || !memory_.Read(batch[j].item + 0x70, parent) ||
                parent != journal_->parents[i + j]) return Result::RejectedStale;
        }
        i += count;
    }
    if ((journal_->records[uint32_t(root)].flags & 1) || journal_->parents[uint32_t(root)] ||
        Key(journal_->records[uint32_t(root)].item, bridge::Page::Journal) != key) return Result::RejectedOwnership;
    document_->key = 0; uint32_t length{};
    if (!CopyNativeDocumentPart(journal_->records[uint32_t(root)].item, length)) return Result::RejectedUnavailable;
    for (int32_t child = journal_->heads[entryIndex]; child >= 0; child = journal_->next[uint32_t(child)])
        if (child >= items.count || !CopyNativeDocumentPart(journal_->records[uint32_t(child)].item, length)) return Result::RejectedUnavailable;
    if (!Roots(controller_, pawn) || pawn != pawn_) return Result::RejectedStale;
    document_->generation = generation_; document_->signature = JournalGroupSignature(entryIndex);
    document_->length = length; document_->key = key;
    return Result::Applied;
}
} // namespace dishonored

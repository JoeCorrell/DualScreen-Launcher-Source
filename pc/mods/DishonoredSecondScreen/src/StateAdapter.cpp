#include "StateAdapter.h"
#include "TargetBuild.h"
#include "ItemCatalog.h"
#include "PortraitCatalog.h"
#include "JournalIntegration.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <cmath>

namespace dishonored {
namespace {
constexpr uint32_t MaximumString = 65536;
constexpr uint64_t HashSeed = 14695981039346656037ull;
void Reset(bridge::Snapshot& snapshot) noexcept {
    // Construct directly in the preallocated mailbox slot: never create a
    // half-megabyte temporary on Dishonored's game-thread stack.
    ::new (static_cast<void*>(&snapshot)) bridge::Snapshot{};
}
uint64_t Hash(uint64_t hash, const void* bytes, std::size_t size) noexcept {
    const auto* data = static_cast<const uint8_t*>(bytes);
    for (std::size_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 1099511628211ull; }
    return hash;
}
template<class T> uint64_t HashValue(uint64_t hash, T value) noexcept { return Hash(hash, &value, sizeof(value)); }
uint64_t HashText(uint64_t hash, const bridge::Snapshot& snapshot, bridge::TextRef text) noexcept {
    hash = HashValue(hash, text.length);
    return bridge::ValidText(snapshot, text) ? Hash(hash, snapshot.text.data() + text.offset, text.length * 2) : hash;
}
bridge::ObjectiveState ObjectiveState(uint8_t state) noexcept {
    // DishonoredObjective.EDisObjectiveState, installed cooked package.
    switch (state) {
        case 0: return bridge::ObjectiveState::Active;
        case 1: return bridge::ObjectiveState::Completed;
        case 2: return bridge::ObjectiveState::Failed;
        default: return bridge::ObjectiveState::Unknown;
    }
}
}

StateAdapter::StateAdapter(Address image, Bridge& bridge, const Definitions* definitions, bool nativeActions)
    : memory_(image, ReadProcessBytes), cleanupMemory_(new Memory(image, ReadProcessBytes)), reflection_(memory_), bridge_(bridge),
      definitions_(definitions), nativeActions_(nativeActions), stringBuffer_(new char16_t[MaximumString]),
      journal_(new JournalWorkspace), document_(new DocumentCache) {}

bool StateAdapter::Roots(Address controller, Address& pawn, Address* world) noexcept {
    Address currentController{}, currentPawn{}, expectedClass{}, actualClass{}, vtable{};
    pawn = 0;
    if (world) *world = 0;
    rootStage_ = RootStage::Quiescence;
    if (!memory_.ObjectsQuiescent()) return false;
    rootStage_ = RootStage::ControllerIdentity;
    if (!memory_.Read(memory_.Image() + ControllerGlobal, currentController) || currentController != controller ||
        !memory_.Read(controller, vtable) || vtable != memory_.Image() + ControllerVtable) return false;
    rootStage_ = RootStage::ControllerActor;
    if (!memory_.LiveActor(controller)) return false;
    rootStage_ = RootStage::PawnLink;
    if (!memory_.Read(controller + ControllerPawn, pawn) || !memory_.Read(memory_.Image() + PawnGlobal, currentPawn) ||
        !pawn || pawn != currentPawn) return false;
    rootStage_ = RootStage::PawnClass;
    if (!memory_.Read(memory_.Image() + PawnClassGlobal, expectedClass) || !expectedClass ||
        !memory_.Object(pawn, actualClass) || actualClass != expectedClass) return false;
    rootStage_ = RootStage::PawnActor;
    if (!memory_.LiveActor(pawn)) return false;
    rootStage_ = RootStage::PawnVtable;
    if (!memory_.Read(pawn, vtable) || vtable != memory_.Image() + PawnVtable) return false;
    rootStage_ = RootStage::WorldIdentity;
    Address currentWorld{};
    if (!memory_.Read(memory_.Image() + 0x103d878, currentWorld) || !memory_.IsA(currentWorld, u"World")) return false;
    if (world) *world = currentWorld;
    else if (world_ && currentWorld != world_) return false;
    rootStage_ = RootStage::Ready;
    return true;
}
bool StateAdapter::FieldAt(Address object, std::u16string_view name, std::u16string_view kind,
    uint32_t bytes, Address& address) noexcept {
    Address type{}; Field field{};
    if (!memory_.Object(object, type) || !reflection_.Resolve(type, name, kind, field) ||
        !field.Contains(bytes) || !Memory::Span(object, field.ownerSize)) return false;
    address = object + field.offset; return true;
}
bridge::AssetRef StateAdapter::ItemIcon(Address definition) noexcept {
    Address field{}; uint32_t length{};
    if (!FieldAt(definition, u"m_JournalIconName", u"StrProperty", 12, field) ||
        !memory_.String(field, stringBuffer_.get(), MaximumString, length)) return {};
    return {ItemArtwork({stringBuffer_.get(), length})};
}
bool StateAdapter::Text(Address string, bridge::Snapshot& snapshot, bridge::TextRef& out) noexcept {
    out = {};
    uint32_t length{};
    if (!memory_.String(string, stringBuffer_.get(), MaximumString, length)) return false;
    if (!length) return true;
    if (snapshot.textUsed > snapshot.text.size() || length + 1 > snapshot.text.size() - snapshot.textUsed) return false;
    out = {snapshot.textUsed, length};
    std::copy_n(stringBuffer_.get(), length + 1, snapshot.text.data() + snapshot.textUsed);
    snapshot.textUsed += length + 1;
    return true;
}
bool StateAdapter::Literal(std::u16string_view text, bridge::Snapshot& snapshot, bridge::TextRef& out) noexcept {
    out = {};
    if (text.empty()) return true;
    if (snapshot.textUsed > snapshot.text.size() || text.size() + 1 > snapshot.text.size() - snapshot.textUsed) return false;
    out = {snapshot.textUsed, uint32_t(text.size())};
    std::copy(text.begin(), text.end(), snapshot.text.begin() + snapshot.textUsed);
    snapshot.textUsed += uint32_t(text.size()); snapshot.text[snapshot.textUsed++] = 0;
    return true;
}
uint64_t StateAdapter::Revision(uint32_t domain, uint64_t hash) noexcept {
    if (!revisions_[domain] || hashes_[domain] != hash) { hashes_[domain] = hash; ++revisions_[domain]; }
    return revisions_[domain];
}
bridge::Key StateAdapter::Key(Address object, bridge::Page page) noexcept {
    int32_t index{};
    if (!memory_.Read(object + layout::ObjectIndex, index) || index < 0) return 0;
    return ((uint64_t(page) + 1) << 32) | (uint32_t(index) + uint64_t(1));
}
void StateAdapter::Scalars(Address pawn, bridge::Snapshot& snapshot) noexcept {
    snapshot.health.known = memory_.Read(pawn + PawnHealth, snapshot.health.value);
    snapshot.maximumHealth.known = memory_.Read(pawn + PawnHealthMax, snapshot.maximumHealth.value);
    snapshot.mana.known = memory_.Read(pawn + PawnMana, snapshot.mana.value);
    snapshot.maximumMana.known = memory_.Read(pawn + PawnManaMax, snapshot.maximumMana.value);
    // BoneCharms resolves the modifier-aware count separately from slot storage.
}
bool StateAdapter::PlayerTweaks(Address pawn, Address& tweaks) noexcept {
    Address field{};
    // Resolve the pawn's real object reference. The native interface can
    // construct a default when absent; a read-only sampler must not do that.
    tweaks = 0;
    return FieldAt(pawn, u"m_pPawnTweaks", u"ObjectProperty", 4, field) &&
        field == pawn + 0x748 && memory_.Read(field, tweaks) && memory_.IsA(tweaks, u"DisTweaks_PlayerPawn");
}
void StateAdapter::Currencies(Address pawn, bridge::Snapshot& snapshot) noexcept {
    Address tweaks{}, inventory{};
    if (!PlayerTweaks(pawn, tweaks) || !memory_.Read(pawn + 0x59c, inventory) ||
        !memory_.IsA(inventory, u"DishonoredInventory")) return;
    struct Currency { Address item{}; bridge::Integer quantity{}; bool found{}; };
    std::array<Currency, 2> currencies{};
    constexpr std::u16string_view fields[] = {u"m_pMoneyItem", u"m_pRuneItem"};
    for (uint32_t i = 0; i < currencies.size(); ++i) {
        Address field{}, item{};
        if (FieldAt(tweaks, fields[i], u"ObjectProperty", 4, field) && memory_.Read(field, item) &&
            memory_.IsA(item, u"DisAbstractItem")) currencies[i].item = item;
    }
    if (!currencies[0].item && !currencies[1].item) return;
    // Read-only equivalent of 00c0b4d0: first matching definition pointer,
    // quantity at +4, and zero only after a successful complete search. Its
    // hidden flag is a separate optional output, not a quantity filter.
    struct Record { Address item; int32_t quantity; uint32_t flags; };
    static_assert(sizeof(Record) == 12);
    GameArray items{}; std::array<Record, 128> batch{};
    if (!memory_.Array(inventory + 0xc8, items, 16384, sizeof(Record))) return;
    bool complete = true;
    for (int32_t base = 0; base < items.count;) {
        const uint32_t count = std::min<uint32_t>(uint32_t(items.count - base), batch.size());
        if (!memory_.Bytes(items.data + uint32_t(base) * sizeof(Record), batch.data(), count * sizeof(Record))) {
            complete = false; break;
        }
        for (uint32_t i = 0; i < count; ++i) for (auto& currency : currencies) {
            if (currency.item && !currency.found && batch[i].item == currency.item) {
                currency.found = true;
                currency.quantity = {batch[i].quantity, batch[i].quantity >= 0};
            }
        }
        base += int32_t(count);
        if ((!currencies[0].item || currencies[0].found) && (!currencies[1].item || currencies[1].found)) break;
    }
    for (auto& currency : currencies)
        if (complete && currency.item && !currency.found) currency.quantity = {0, true};
    snapshot.coins = currencies[0].quantity; snapshot.runes = currencies[1].quantity;
}
bool StateAdapter::CharmSlots(Address pawn, Address tweaks, int32_t& count) noexcept {
    Address field{}, attributes{}, map{}; int32_t base{}; float modifier{}; GameName name{};
    // Interface getter 00b49270 receives pawn+4a8 and returns [this+2d4]
    // (pawn+77c). 00aa8b50 adds the truncating conversion of the named
    // attribute to tweak+210. Dirty modifiers use the getter's verified
    // arithmetic in a read-only evaluator, without refreshing engine caches.
    if (!FieldAt(tweaks, u"m_MaxActivatedBoneCharmCount", u"IntProperty", 4, field) || field != tweaks + 0x210 ||
        !memory_.Read(field, base) || !FieldAt(pawn, u"m_pAttributes", u"ObjectProperty", 4, field) || field != pawn + 0x77c ||
        !memory_.Read(field, attributes) || !memory_.IsA(attributes, u"DisAttributes") ||
        !FieldAt(attributes, u"m_ModifiedAttributes", u"MapProperty", 0x3c, map) || map != attributes + 0x38 ||
        !memory_.Read(memory_.Image() + 0x1051824, name) || !memory_.CachedAttribute(map, name, modifier) ||
        double(modifier) < double(INT32_MIN) || double(modifier) > double(INT32_MAX)) return false;
    const int64_t total = int64_t(base) + int32_t(modifier);
    // Cooked m_WhaleBoneCharmSlots has ArrayDim 10. Validate rather than
    // clamping an invalid count into an apparently usable set of slots.
    if (total < 0 || total > 10) return false;
    count = int32_t(total); return true;
}
void StateAdapter::BoneCharms(Address pawn, bridge::Snapshot& snapshot) noexcept {
    Address tweaks{}, field{}, definitions{};
    if (!PlayerTweaks(pawn, tweaks) ||
        !FieldAt(tweaks, u"m_pCharmListTweaks", u"ObjectProperty", 4, field) || field != tweaks + 0x714 ||
        !memory_.Read(field, definitions) || !memory_.IsA(definitions, u"DisTweaks_WhaleBoneCharmList")) return;
    struct Owned { uint8_t level, padding[3]; int32_t infoIndex; };
    static_assert(sizeof(Owned) == 8);
    GameArray owned{};
    if (!memory_.Array(pawn + 0xb38, owned, 4096, sizeof(Owned))) return;
    auto& collection = snapshot.charms;
    collection.domain.total = uint32_t(owned.count);
    std::array<int32_t, 10> slots{}; int32_t slotCount{};
    bool slotsKnown = CharmSlots(pawn, tweaks, slotCount) &&
        FieldAt(pawn, u"m_WhaleBoneCharmSlots", u"IntProperty", sizeof(slots), field) && field == pawn + 0xb10 &&
        memory_.Bytes(field, slots.data(), uint32_t(slotCount) * 4);
    for (int32_t i = 0; slotsKnown && i < slotCount; ++i) {
        if (slots[i] < -1 || slots[i] >= owned.count) { slotsKnown = false; break; }
        if (slots[i] != -1 && std::find(slots.begin(), slots.begin() + i, slots[i]) != slots.begin() + i) slotsKnown = false;
    }
    if (slotsKnown) {
        snapshot.charmSlots = {slotCount, true};
        for (int32_t i = 0; i < slotCount; ++i)
            snapshot.equippedCharms[uint32_t(i)] = {slots[i] == -1 ? 0 :
                (uint64_t(0x200 + uint32_t(bridge::Page::BoneCharms)) << 32) | uint64_t(uint32_t(slots[i] + 1)), true};
    }
    const auto eligibility = NativeInput(0x10);
    const bool spareSlot = slotsKnown && std::find(slots.begin(), slots.begin() + slotCount, -1) != slots.begin() + slotCount;
    bool complete = true; uint64_t hash = HashSeed;
    // The game's journal (00bd2460) displays the collected records in reverse
    // acquisition order. Definition arrays alone never confer ownership.
    for (int32_t i = owned.count; i > 0; --i) {
        if (collection.domain.stored >= collection.entries.size()) { complete = false; break; }
        Owned record{};
        if (!memory_.Read(owned.data + uint32_t(i - 1) * sizeof(Owned), record) || record.infoIndex < 0) {
            complete = false; break;
        }
        const bool scripted = record.infoIndex >= 10000000;
        const int32_t index = scripted ? record.infoIndex - 10000000 : record.infoIndex;
        GameArray infos{}, levels{};
        // 00aa8bb0 selects +8c or +98, stride14; 00bd2539 resolves
        // Levels at info+8, stride1c. Localized FStrings are level+0/+c;
        // level+18 is the gameplay effect float, not description text.
        if (!FieldAt(definitions, scripted ? u"m_ScriptedCharms" : u"m_Charms", u"ArrayProperty", 12, field) ||
            field != definitions + (scripted ? 0x98 : 0x8c) ||
            !memory_.Array(field, infos, 65536, 0x14) || index >= infos.count ||
            !memory_.Array(infos.data + uint32_t(index) * 0x14 + 8, levels, 256, 0x1c) || record.level >= levels.count) {
            complete = false; break;
        }
        const Address level = levels.data + uint32_t(record.level) * 0x1c;
        auto& entry = collection.entries[collection.domain.stored];
        if (!Text(level, snapshot, entry.label.title) || !Text(level + 0xc, snapshot, entry.label.description)) {
            complete = false; break;
        }
        entry.key = (uint64_t(0x200 + uint32_t(bridge::Page::BoneCharms)) << 32) | uint64_t(uint32_t(i));
        entry.tier = {record.level, true}; // Native zero-based charm level.
        entry.label.artwork = {ItemArtwork(u"BoneCharms")};
        if (slotsKnown) {
            const auto found = std::find(slots.begin(), slots.begin() + slotCount, i - 1);
            const bool equipped = found != slots.begin() + slotCount;
            entry.equipped = {equipped, true};
            entry.equippedSlot = {equipped ? int32_t(found - slots.begin()) : -1, true};
            entry.canEquip = {nativeActions_ && eligibility.value && !equipped && spareSlot, eligibility.known};
            entry.canRemove = {nativeActions_ && eligibility.value && equipped, eligibility.known};
        }
        hash = HashValue(hash, entry.key); hash = HashValue(hash, record.infoIndex);
        hash = HashValue(hash, record.level); hash = HashText(hash, snapshot, entry.label.title);
        hash = HashText(hash, snapshot, entry.label.description);
        hash = HashValue(hash, entry.equipped.known); hash = HashValue(hash, entry.equippedSlot.value);
        hash = HashValue(hash, entry.canEquip.known); hash = HashValue(hash, entry.canEquip.value);
        hash = HashValue(hash, entry.canRemove.value);
        ++collection.domain.stored;
    }
    collection.domain.availability = complete && slotsKnown && eligibility.known && !memory_.Failed() ?
        bridge::Availability::Available : bridge::Availability::Incomplete;
    hash = HashValue(hash, collection.domain.availability);
    hash = HashValue(hash, complete); hash = HashValue(hash, collection.domain.total);
    hash = HashValue(hash, slotsKnown); hash = HashValue(hash, slotsKnown ? slotCount : -1);
    for (const auto& slot : snapshot.equippedCharms) hash = HashValue(hash, slot.value);
    collection.domain.revision = Revision(uint32_t(bridge::Page::BoneCharms), hash);
}
bool StateAdapter::SelectedPower(Address pawn, int32_t& index) noexcept {
    // Read-only counterpart of 00b505e0 -> inventory 00c04820(usage 2) ->
    // DisItemPowers::00c44750. The latter remaps m_iPowerSlot through the
    // pawn's usable-active array; it is not itself the active-component index.
    Address inventory{}, item{}; uint8_t flags{}; int32_t slot{}, powerSlot{};
    GameArray slots{}, usable{};
    index = -1;
    if (!memory_.Read(pawn + 0x59c, inventory) || !memory_.IsA(inventory, u"DishonoredInventory") ||
        !memory_.Read(inventory + 0xe0, flags)) return false;
    if (!(flags & 1)) return true;
    if (!memory_.Read(inventory + 0x44 + 2 * 0x28, slot)) return false;
    if (slot == -1) return true;
    if (!memory_.Array(inventory + 0x38, slots, 256, 12) || slot < 0 || slot >= slots.count ||
        !memory_.Read(slots.data + uint32_t(slot) * 12, item)) return false;
    if (!item) return true;
    // 00aa5310's class global 01465128 is initialized from the DisItemPowers
    // registration at 01367d90; do not infer the type from the current icon.
    if (!memory_.IsA(item, u"DisItemPowers")) return !memory_.Failed();
    if (!memory_.Read(item + 0x118, powerSlot)) return false;
    if (powerSlot == -1) return true;
    if (!memory_.Array(pawn + PawnActivePowers, usable, 64, 4) || powerSlot < 0 || powerSlot >= usable.count) return false;
    return memory_.Read(usable.data + uint32_t(powerSlot) * 4, index);
}
void StateAdapter::Powers(Address pawn, bridge::Snapshot& snapshot) noexcept {
    if (!definitions_) return;
    auto& collection = snapshot.powers;
    GameArray active{}, usable{}; std::array<int32_t, 64> usableIndices{};
    if (!memory_.Array(pawn + 0x788, active, 64, 4) ||
        !memory_.Array(pawn + PawnActivePowers, usable, usableIndices.size(), 4) ||
        !memory_.Bytes(usable.data, usableIndices.data(), uint32_t(usable.count) * 4)) return;
    int32_t selected{}; const bool selectedKnown = SelectedPower(pawn, selected);
    const auto eligibility = NativeInput(0x800); uint8_t powerFlags{};
    const bool powerFlagsKnown = memory_.Read(pawn + 0x8a0, powerFlags);
    Address game{}, gameClass{}, baseGameClass{};
    // 00bbf2b0 disables the random free-cast branch of 00bf3020 for the
    // exact base-game class. A subclass must establish its own cost contract.
    const bool baseGame = GameInfo(game) && memory_.Object(game, gameClass) &&
        memory_.Read(memory_.Image() + 0x1051008, baseGameClass) && baseGameClass && gameClass == baseGameClass;
    uint64_t hash = HashSeed; bool complete = true;
    for (int32_t i = 0; i < active.count; ++i) {
        Address component{}, field{}, tweaks{}; uint8_t selection{}; int32_t level{};
        if (!memory_.Read(active.data + uint32_t(i) * 4, component) ||
            !memory_.IsA(component, u"DishonoredActivePowerComponent") ||
            !memory_.Read(component + 0x74, selection) || !memory_.Read(component + 0x88, level)) { complete = false; break; }
        if (level < 0 || level > 2) { complete = false; break; }
        auto name = definitions_->PowerName(selection);
        if (name.empty()) continue;
        if (collection.domain.stored >= collection.entries.size()) { complete = false; break; }
        const bool owned = std::find(usableIndices.begin(), usableIndices.begin() + usable.count, i) != usableIndices.begin() + usable.count;
        auto& entry = collection.entries[collection.domain.stored];
        entry.key = Key(component, bridge::Page::Powers);
        if (!entry.key || !Literal(name, snapshot, entry.label.title) ||
            !Literal(definitions_->PowerDescription(selection, owned ? level : 1), snapshot, entry.label.description)) { complete = false; break; }
        entry.label.artwork.id = definitions_->PowerArtwork(selection);
        entry.unlocked = {owned, true}; entry.tier = {owned ? level : 0, true};
        entry.selected = {selectedKnown && selected == i, selectedKnown};
        entry.selectable = {nativeActions_ && owned && eligibility.value && !(powerFlags & 0x10), eligibility.known && powerFlagsKnown};
        if (FieldAt(component, u"m_pPowerTweaks", u"ObjectProperty", 4, field) && memory_.Read(field, tweaks) &&
            memory_.IsA(tweaks, u"DisTweaks_ActivePowerBase")) {
            int32_t costs[2]{};
            // Native journal reads the two m_RuneCosts at +90/+94; cooked
            // UProperty metadata independently confirms ArrayDim == 2.
            if (FieldAt(tweaks, u"m_RuneCosts", u"IntProperty", sizeof(costs), field) && field == tweaks + 0x90 &&
                memory_.Bytes(field, costs, sizeof(costs))) {
                entry.maximumTier = {2, true};
                const int32_t tier = owned ? level : 0;
                if (tier >= 0 && tier < 2 && costs[tier] >= 0) entry.runeRequirement = {costs[tier], true};
            }
            if (FieldAt(tweaks, u"m_ManaCost", u"IntProperty", 4, field) && field == tweaks + 0x8c)
                entry.baseManaCost.known = memory_.Read(field, entry.baseManaCost.value) && entry.baseManaCost.value >= 0;
            Address table{}, consume{}, owner{}, playerOwner{};
            if (entry.baseManaCost.known && baseGame && memory_.Read(component, table) &&
                memory_.Read(table + 0x178, consume) && consume == memory_.Image() + 0x7f3020 &&
                memory_.Read(component + 0x7c, owner) && owner == pawn &&
                memory_.Read(component + 0x80, playerOwner) && playerOwner == pawn)
                entry.manaCost = entry.baseManaCost;
        }
        // An unverified consumption override retains only the definition cost.
        hash = HashValue(hash, entry.key); hash = HashValue(hash, selection); hash = HashValue(hash, level);
        hash = HashValue(hash, owned); hash = HashValue(hash, entry.selected.value); hash = HashValue(hash, selectedKnown);
        hash = HashValue(hash, entry.runeRequirement.known); hash = HashValue(hash, entry.runeRequirement.value);
        hash = HashValue(hash, entry.baseManaCost.known); hash = HashValue(hash, entry.baseManaCost.value);
        hash = HashValue(hash, entry.manaCost.known); hash = HashValue(hash, entry.manaCost.value);
        hash = HashValue(hash, entry.selectable.known); hash = HashValue(hash, entry.selectable.value);
        ++collection.domain.stored;
    }
    // Passive powers are an independent component array: native journal
    // 00bc9b50 reads +54/stride1c, and uses stored levels when inhibited.
    Address passiveComponent{};
    if (memory_.Read(pawn + 0xa38, passiveComponent) && memory_.IsA(passiveComponent, u"DishonoredPowersComponent")) {
        struct Passive { GameName name; uint8_t selection{}, padding[3]{}; GameArray levels; int32_t level{}; };
        static_assert(sizeof(Passive) == 0x1c);
        GameArray powers{}, passiveUsable{}, stored{}; uint8_t inhibited{};
        std::array<int32_t, 64> passiveIndices{}, storedLevels{};
        if (memory_.Array(passiveComponent + 0x54, powers, 64, sizeof(Passive)) &&
            memory_.Array(pawn + 0xa4c, passiveUsable, 64, 4) &&
            memory_.Bytes(passiveUsable.data, passiveIndices.data(), uint32_t(passiveUsable.count) * 4) &&
            memory_.Read(passiveComponent + 0x6c, inhibited)) {
            bool storedKnown = !(inhibited & 1) || (memory_.Array(passiveComponent + 0x60, stored, 64, 4) &&
                stored.count >= powers.count && memory_.Bytes(stored.data, storedLevels.data(), uint32_t(stored.count) * 4));
            for (int32_t i = 0; storedKnown && i < powers.count; ++i) {
                Passive record{};
                if (!memory_.Read(powers.data + uint32_t(i) * sizeof(Passive), record)) { complete = false; break; }
                auto name = definitions_->PowerName(record.selection);
                if (name.empty()) continue;
                if (collection.domain.stored >= collection.entries.size()) { complete = false; break; }
                bool usableNow = std::find(passiveIndices.begin(), passiveIndices.begin() + passiveUsable.count, i) != passiveIndices.begin() + passiveUsable.count;
                int32_t level = (inhibited & 1) ? storedLevels[i] : usableNow ? record.level : 0;
                GameArray levels{};
                if (!memory_.Array(powers.data + uint32_t(i) * sizeof(Passive) + 0xc, levels, 32, 16) ||
                    !levels.count || level < -1 || level >= levels.count) { complete = false; break; }
                auto& entry = collection.entries[collection.domain.stored];
                entry.key = (uint64_t(0x100 + uint32_t(bridge::Page::Powers)) << 32) | (uint64_t(uint32_t(i)) + 1);
                entry.passive = true; entry.unlocked = {level > 0, true}; entry.tier = {std::max(0, level), true};
                entry.label.artwork.id = definitions_->PowerArtwork(record.selection);
                entry.selected = {false, true}; entry.selectable = {false, true};
                if (!Literal(name, snapshot, entry.label.title) || !Literal(definitions_->PowerDescription(record.selection, level), snapshot, entry.label.description)) { complete = false; break; }
                entry.maximumTier = {levels.count - 1, true};
                int32_t next = std::max(0, level) + 1;
                if (next < levels.count) entry.runeRequirement.known = memory_.Read(levels.data + uint32_t(next) * 16, entry.runeRequirement.value) && entry.runeRequirement.value >= 0;
                hash = HashValue(hash, entry.key); hash = HashValue(hash, record.selection); hash = HashValue(hash, level);
                hash = HashValue(hash, entry.runeRequirement.known); hash = HashValue(hash, entry.runeRequirement.value);
                ++collection.domain.stored;
            }
            if (!storedKnown) complete = false;
        } else complete = false;
    } else complete = false;
    collection.domain.total = collection.domain.stored;
    collection.domain.availability = bridge::Availability::Incomplete;
    collection.domain.revision = Revision(uint32_t(bridge::Page::Powers), HashValue(hash, complete));
}
void StateAdapter::Objectives(Address controller, bridge::Snapshot& snapshot) noexcept {
    auto& collection = snapshot.objectives;
    Address component{}, type{};
    // Journal's 00bd7810 reads this persistent controller component, not a
    // movie-owned objective cache. Array and filter offsets traced there and
    // in native accessors 00ac1220/30/40/50.
    if (!memory_.Read(controller + 0x6f8, component) || !memory_.Object(component, type) ||
        !memory_.IsA(component, u"DishonoredObjectivesComponent")) return;
    GameArray objectives{};
    if (!memory_.Array(component + 0x54, objectives, 4096, 4)) return;
    Address chapter{}; bool chapterDetailsComplete{};
    const bool chapterKnown = CurrentChapter(chapter, snapshot, chapterDetailsComplete);
    ObjectiveMarkers(snapshot);
    bool complete = chapterKnown && chapterDetailsComplete;
    uint64_t hash = HashSeed;
    hash = HashText(hash, snapshot, snapshot.chapter.title);
    hash = HashText(hash, snapshot, snapshot.chapter.description);
    for (const auto& target : snapshot.chapterTargets) {
        hash = HashValue(hash, target.label.artwork.id);
        hash = HashText(hash, snapshot, target.label.title);
        hash = HashValue(hash, target.state.known); hash = HashValue(hash, target.state.value);
    }
    for (int32_t i = 0; i < objectives.count; ++i) {
        Address objective{}; uint8_t state{}; uint32_t flags{};
        if (!memory_.Read(objectives.data + uint32_t(i) * 4, objective) ||
            !memory_.IsA(objective, u"DishonoredObjective") ||
            !memory_.Read(objective + 0x45, state) || !memory_.Read(objective + 0x48, flags)) { complete = false; break; }
        // Journal 00bd7810 with history enabled. Hidden objectives never
        // appear; inactive objectives with a chapter list require membership.
        if (flags & 0x10) continue;
        if (state > 2) { complete = false; continue; }
        if (state != 0) {
            GameArray chapters{}; std::array<Address, 256> allowed{};
            if (!memory_.Array(objective + 0x4c, chapters, allowed.size(), 4) ||
                !memory_.Bytes(chapters.data, allowed.data(), uint32_t(chapters.count) * 4)) { complete = false; continue; }
            if (chapters.count && (!chapterKnown || std::find(allowed.begin(), allowed.begin() + chapters.count, chapter) ==
                allowed.begin() + chapters.count)) continue;
        }
        ++collection.domain.total;
        if (collection.domain.stored >= collection.entries.size()) { complete = false; continue; }
        auto& entry = collection.entries[collection.domain.stored];
        Address description{};
        if (!FieldAt(objective, u"m_Description", u"StrProperty", 12, description) ||
            !Text(description, snapshot, entry.label.description)) { complete = false; break; }
        // A generation-scoped adapter key, never dereferenced by the panel.
        entry.key = Key(objective, bridge::Page::Objectives);
        if (!entry.key) { complete = false; break; }
        entry.label.title = entry.label.description;
        entry.state = ObjectiveState(state);
        entry.optional = {(flags & 2) != 0, true};
        entry.tracked = {state == 0 && (flags & 0x20) != 0, true};
        hash = HashValue(hash, entry.key);
        hash = HashValue(hash, state); hash = HashValue(hash, flags);
        hash = HashText(hash, snapshot, entry.label.description);
        ++collection.domain.stored;
        if (!ObjectiveTasks(objective, entry.key, state == 0, entry.tracked.value, entry.optional.value, snapshot, hash)) complete = false;
    }
    // The installed package declares Base and Custom task classes; their
    // status virtuals are verified below. Unknown overrides, failed chapter
    // details, missing observations and partial reads remain incomplete.
    collection.domain.availability = complete && !memory_.Failed() &&
        snapshot.objectiveTargets.domain.availability == bridge::Availability::Available ?
        bridge::Availability::Available : bridge::Availability::Incomplete;
    hash = HashValue(hash, collection.domain.availability);
    hash = HashValue(hash, complete); hash = HashValue(hash, collection.domain.total);
    hash = HashValue(hash, snapshot.objectiveTargets.domain.availability);
    hash = HashValue(hash, snapshot.objectiveTargets.domain.total);
    hash = HashValue(hash, snapshot.objectiveTargets.domain.stored);
    collection.domain.revision = Revision(uint32_t(bridge::Page::Objectives), hash);
    snapshot.objectiveTargets.domain.revision = collection.domain.revision;
}
bool StateAdapter::CurrentChapter(Address& chapter, bridge::Snapshot& snapshot, bool& detailsComplete) noexcept {
    chapter = 0; detailsComplete = false;
    Address world{}, persistentInfo{}, game{}, info{}, map{}, field{}, list{};
    if (!memory_.Read(memory_.Image() + 0x103d878, world) || !memory_.IsA(world, u"World") ||
        !memory_.Read(world + 0x2c0, persistentInfo) || !memory_.IsA(persistentInfo, u"WorldInfo") ||
        !memory_.Read(persistentInfo + 0x410, game) || !memory_.IsA(game, u"DishonoredGameInfo") ||
        !memory_.Read(world + 0x2c4, info) || !memory_.IsA(info, u"WorldInfo")) return false;
    // WorldInfo::GetMapInfo 0078d5c0 prefers the first streamed level's
    // WorldInfo when it is available, then falls back to this WorldInfo.
    GameArray streaming{};
    if (!memory_.Array(info + 0x314, streaming, 4096, 4)) return false;
    Address selectedInfo = info;
    if (streaming.count) {
        Address level{}, loaded{};
        if (!memory_.Read(streaming.data, level)) return false;
        if (level) {
            if (!memory_.IsA(level, u"LevelStreaming") || !memory_.Read(level + 0x40, loaded)) return false;
            if (loaded) {
                GameArray actors{};
                if (!memory_.IsA(loaded, u"Level") || !memory_.Array(loaded + 0x38, actors, 262144, 4) ||
                    !actors.count || !memory_.Read(actors.data, selectedInfo) || !memory_.IsA(selectedInfo, u"WorldInfo")) return false;
            }
        }
    }
    if (!memory_.Read(selectedInfo + 0x4a8, map) || !memory_.IsA(map, u"DishonoredMapInfo") ||
        !FieldAt(map, u"m_pChapters", u"ObjectProperty", 4, field) || field != map + 0xac ||
        !memory_.Read(field, list)) return false;
    if (!list) { detailsComplete = true; return true; }
    if (!memory_.IsA(list, u"DisTweaks_ChapterInfoList")) return false;
    struct Entry { GameName tag; Address chapter{}; };
    GameArray chapters{}; GameName current{};
    if (!FieldAt(game, u"m_CurrentChapterTag", u"NameProperty", 8, field) || field != game + 0x44c ||
        !memory_.Read(field, current) || !memory_.Array(list + 0x8c, chapters, 1024, sizeof(Entry))) return false;
    for (int32_t i = 0; i < chapters.count; ++i) {
        Entry entry{};
        if (!memory_.Read(chapters.data + uint32_t(i) * sizeof(Entry), entry)) return false;
        if (entry.tag.index != current.index || entry.tag.number != current.number) continue;
        if (!entry.chapter) { detailsComplete = true; return true; }
        if (!memory_.IsA(entry.chapter, u"DisTweaks_ChapterInfo") ||
            !FieldAt(entry.chapter, u"m_ChapterTitle", u"StrProperty", 12, field) ||
            !Text(field, snapshot, snapshot.mission)) return false;
        chapter = entry.chapter;
        snapshot.chapter.title = snapshot.mission;
        detailsComplete = FieldAt(chapter, u"m_ChapterDescription", u"StrProperty", 12, field) && field == chapter + 0x98 &&
            Text(field, snapshot, snapshot.chapter.description);
        if (!ChapterTargets(game, chapter, snapshot)) detailsComplete = false;
        return true;
    }
    detailsComplete = true;
    return true;
}
bool StateAdapter::ChapterTargets(Address game, Address chapter, bridge::Snapshot& snapshot) noexcept {
    Address stateField{}, definitions{};
    // 009fac50 returns only current target records with a nonempty portrait.
    // 00bc0a70 publishes those records, not all chapter target definitions.
    if (!FieldAt(game, u"m_CurrentChapterTargets", u"StructProperty", 32, stateField) || stateField != game + 0x454 ||
        !FieldAt(chapter, u"m_ChapterTargets", u"ObjectProperty", 8, definitions) || definitions != chapter + 0xa4) return false;
    bool complete = true;
    for (uint32_t i = 0; i < 2; ++i) {
        const Address record = stateField + i * 16;
        uint32_t length{}; uint8_t state{};
        if (!memory_.String(record, stringBuffer_.get(), MaximumString, length)) { complete = false; continue; }
        if (!length) continue; // Native absence, not a failed target read.
        if (!memory_.Read(record + 12, state) || state > 4) { complete = false; continue; }
        const auto portrait = PortraitArtwork({stringBuffer_.get(), length});
        if (!portrait) { complete = false; continue; }
        auto& target = snapshot.chapterTargets[i];
        target.label.artwork = {portrait}; target.state = {bridge::TargetState(state), true};
        Address definition{}, field{};
        if (!memory_.Read(definitions + i * 4, definition) || !memory_.IsA(definition, u"DisTweaks_ChapterTarget") ||
            !FieldAt(definition, u"m_TargetPortraitPath", u"StrProperty", 12, field) ||
            !memory_.String(field, stringBuffer_.get(), MaximumString, length)) { complete = false; continue; }
        if (PortraitArtwork({stringBuffer_.get(), length}) != portrait) continue;
        // An unknown/overridden portrait must not acquire a secret name just
        // because its chapter points at a known assassination target.
        if (!FieldAt(definition, u"m_TargetName", u"StrProperty", 12, field) || !Text(field, snapshot, target.label.title)) complete = false;
    }
    return complete;
}
bool StateAdapter::ObjectiveTasks(Address objective, bridge::Key parent, bool objectiveActive, bool objectiveTracked, bool optional,
    bridge::Snapshot& snapshot, uint64_t& hash) noexcept {
    GameArray tasks{};
    if (!memory_.Array(objective + 0x58, tasks, 4096, 4)) return false;
    auto& collection = snapshot.objectives; bool complete = true;
    for (int32_t i = 0; i < tasks.count; ++i) {
        Address task{}, description{}, table{}, getter{}; uint8_t state{}; uint32_t flags{};
        if (!memory_.Read(tasks.data + uint32_t(i) * 4, task) || !memory_.IsA(task, u"DishonoredTask_Base") ||
            !memory_.Read(task + 0x5d, state) || !memory_.Read(task + 0x60, flags)) return false;
        if (flags & 8) continue;
        if (state > 3) { complete = false; continue; }
        const bool finished = state == 2 || state == 3;
        // Native history includes completed/failed tasks; nonterminal tasks
        // are shown only while their containing objective is active.
        if (!objectiveActive && !finished) continue;
        ++collection.domain.total;
        if (collection.domain.stored >= collection.entries.size()) { complete = false; continue; }
        auto& entry = collection.entries[collection.domain.stored];
        if (!FieldAt(task, u"m_Description", u"StrProperty", 12, description) || description != task + 0x38 ||
            !Text(description, snapshot, entry.label.description)) { complete = false; continue; }
        entry.key = Key(task, bridge::Page::Objectives); entry.parent = parent;
        if (!entry.key) { complete = false; continue; }
        entry.label.title = entry.label.description;
        entry.state = finished ? state == 2 ? bridge::ObjectiveState::Completed : bridge::ObjectiveState::Failed : bridge::ObjectiveState::Active;
        entry.optional = {optional || (flags & 2) != 0, true};
        entry.tracked = {objectiveTracked && !finished && (flags & 0x10) != 0, true};
        // Base GetStatus returns an empty FString. Custom task 00ACBE10 copies
        // its already evaluated m_ActiveStatus at +7c; never display the raw
        // m_StatusFormat template or call a getter with engine allocations.
        if (!memory_.Read(task, table) || !memory_.Read(table + 0x12c, getter)) complete = false;
        else if (getter == memory_.Image() + 0x6cbe10) {
            Address status{};
            if (!memory_.IsA(task, u"DishonoredTask_Custom") ||
                !FieldAt(task, u"m_ActiveStatus", u"StrProperty", 12, status) || status != task + 0x7c ||
                !Text(status, snapshot, entry.status)) complete = false;
        } else if (getter != memory_.Image() + 0x52dc90) complete = false;
        hash = HashValue(hash, entry.key); hash = HashValue(hash, parent);
        hash = HashValue(hash, state); hash = HashValue(hash, flags);
        hash = HashText(hash, snapshot, entry.label.description);
        hash = HashText(hash, snapshot, entry.status);
        ++collection.domain.stored;
        if (entry.tracked.value && !ObjectiveTaskTargets(task, entry.key, snapshot, hash)) complete = false;
    }
    return complete;
}
void StateAdapter::ObserveTaskMarker(Address marker, Address actor, int32_t index, float distance,
    uint32_t flags, uint64_t now) noexcept {
    if (lifecycle_ != bridge::Lifecycle::Live || !controller_ || !pawn_) return;
    if (markerObservations_ < uint32_t(INT32_MAX)) ++markerObservations_;
    // Eight probes cap work even for pathological native marker counts.
    // Eviction loses an observation, never invents a target or a distance.
    const uint32_t start = ((marker >> 4) * uint32_t(2654435761)) >> 24;
    uint32_t chosen = start;
    for (uint32_t probe = 0; probe < 8; ++probe) {
        const uint32_t slot = (start + probe) & 255;
        const auto& previous = taskMarkers_[slot];
        if (!previous.marker || previous.marker == marker) { chosen = slot; break; }
        if (previous.at < taskMarkers_[chosen].at) chosen = slot;
    }
    taskMarkers_[chosen] = {marker, actor, index, distance, flags, now};
}
void StateAdapter::ObjectiveMarkers(bridge::Snapshot& snapshot) noexcept {
    taskMarkersKnown_ = false; ownedTaskMarkerCount_ = 0;
    auto& domain = snapshot.objectiveTargets.domain;
    Address game{}, manager{}, hud{}, field{}, table{}; uint32_t settings{};
    // Resolve the current HUD from its owning manager on each sample. Never
    // keep a movie or heap marker pointer across a level/save transition.
    if (!GameInfo(game) || !FieldAt(game, u"m_pGlobalUIManager", u"ObjectProperty", 4, field) ||
        !memory_.Read(field, manager) || !memory_.IsA(manager, u"DisGlobalUIManager") ||
        !FieldAt(manager, u"m_pHUD", u"ObjectProperty", 4, field) || !memory_.Read(field, hud) ||
        !memory_.IsA(hud, u"DisGFxMoviePlayerHUD") || !memory_.Read(hud, table) ||
        table != memory_.Image() + 0xd51fc0 || !memory_.Read(hud + 0x1c0, settings)) return;
    domain.availability = bridge::Availability::Available;
    taskMarkersKnown_ = true;
    // BAD4B0 respects the player's objective-marker setting before querying
    // live tasks. A retained movie array cannot override a newly disabled HUD.
    if (!(settings & 0x80)) return;
    GameArray markers{}; std::array<Address, 256> pointers{};
    if (!FieldAt(hud, u"m_TaskMarkers", u"ArrayProperty", 12, field) || field != hud + 0x524 ||
        !memory_.Array(field, markers, pointers.size(), 4) ||
        !memory_.Bytes(markers.data, pointers.data(), uint32_t(markers.count) * 4)) {
        taskMarkersKnown_ = false; domain.availability = bridge::Availability::Incomplete; return;
    }
    for (int32_t i = 0; i < markers.count; ++i) {
        const Address marker = pointers[i];
        if (!marker) { domain.availability = bridge::Availability::Incomplete; continue; }
        const uint32_t start = ((marker >> 4) * uint32_t(2654435761)) >> 24;
        const MarkerObservation* found = nullptr;
        for (uint32_t probe = 0; probe < 8; ++probe) {
            const auto& candidate = taskMarkers_[(start + probe) & 255];
            if (candidate.marker == marker) { found = &candidate; break; }
        }
        if (!found || found->at > snapshot.sampledAt ||
            snapshot.sampledAt - found->at > bridge::FreshnessMilliseconds) {
            domain.availability = bridge::Availability::Incomplete; continue;
        }
        if (found->flags & 1) ownedTaskMarkers_[ownedTaskMarkerCount_++] = *found;
    }
}
bool StateAdapter::ObjectiveTaskTargets(Address task, bridge::Key parent,
    bridge::Snapshot& snapshot, uint64_t& hash) noexcept {
    if (!taskMarkersKnown_) return false;
    auto& collection = snapshot.objectiveTargets;
    GameArray targets{}; Address field{};
    // AD9240 has already been matched by the objective/task filters above.
    // Its 36-byte records are live task state, never level-template targets.
    if (!FieldAt(task, u"m_Targets", u"ArrayProperty", 12, field) || field != task + 0x68 ||
        !memory_.Array(field, targets, 256, 0x24)) return false;
    bool complete = true;
    for (int32_t i = 0; i < targets.count; ++i) {
        Address actor{}; uint32_t optional{};
        const Address record = targets.data + uint32_t(i) * 0x24;
        if (!memory_.Read(record, actor) || !memory_.Read(record + 0x20, optional)) { complete = false; break; }
        if (!actor) continue;
        const auto end = ownedTaskMarkers_.begin() + ownedTaskMarkerCount_;
        const auto found = std::find_if(ownedTaskMarkers_.begin(), end,
            [actor](const MarkerObservation& marker) { return marker.actor == actor; });
        if (found == end) continue;
        if (!memory_.Registered(actor, found->index) || !memory_.IsA(actor, u"Actor") || !memory_.LiveActor(actor)) continue;
        uint8_t actorFlags{}; uint32_t actorState{};
        if (!memory_.Read(actor + 0x10a, actorFlags) || !memory_.Read(actor + 0x120, actorState)) { complete = false; continue; }
        // BAD4B0 updates marker+28 from bHidden only for actors opting into
        // this visibility rule. Recheck now, before trusting a prior render.
        if ((actorFlags & 0x20) && (actorState & 2)) continue;
        Address table{}, currentActor{}, clip{}; uint32_t renderFlags{}, clipType{};
        if (!memory_.Read(found->marker, table) || table != memory_.Image() + TaskMarkerVtable ||
            !memory_.Read(found->marker + 8, currentActor) || currentActor != actor ||
            !memory_.Read(found->marker + 0x1c, clipType) || (clipType & 0x8f) == 1 ||
            !memory_.Read(found->marker + 0x20, clip) || !clip ||
            !memory_.Read(found->marker + 0x30, renderFlags) || !(renderFlags & 1)) continue;
        ++collection.domain.total;
        if (collection.domain.stored == collection.entries.size()) { complete = false; continue; }
        auto& target = collection.entries[collection.domain.stored];
        // BCE670 localizes the record's file/section m_TargetName into +38.
        // Use the actual localized label; no allocations/localizer calls here.
        if (!Text(found->marker + 0x38, snapshot, target.label.title) || !target.label.title.length) { complete = false; continue; }
        target.task = parent; target.observedAt = found->at; target.optional = (optional & 1) != 0;
        // Actor visibility, ownership, lifecycle and native marker identity
        // were validated above. Copy coordinates only after those predicates.
        std::array<float, 3> location{}; Address locationField{};
        if (FieldAt(actor, u"Location", u"StructProperty", sizeof(location), locationField) &&
            memory_.Bytes(locationField, location.data(), sizeof(location)) &&
            std::isfinite(location[0]) && std::isfinite(location[1]) && std::isfinite(location[2])) {
            target.worldX = {location[0], true}; target.worldY = {location[1], true};
            target.worldZ = {location[2], true};
        }
        const float metres = found->distance * 0.01f;
        if (std::isfinite(metres) && metres >= 0 && double(metres) <= double(INT32_MAX))
            target.distanceMetres = {int32_t(metres), true};
        hash = HashValue(hash, parent); hash = HashValue(hash, target.optional);
        hash = HashText(hash, snapshot, target.label.title);
        hash = HashValue(hash, target.distanceMetres.known); hash = HashValue(hash, target.distanceMetres.value);
        ++collection.domain.stored;
    }
    if (!complete) collection.domain.availability = bridge::Availability::Incomplete;
    return complete;
}
bool StateAdapter::AbstractInventory(Address pawn, bridge::Snapshot& snapshot) noexcept {
    Address inventory{}, type{};
    if (!memory_.Read(pawn + 0x59c, inventory) || !memory_.Object(inventory, type) ||
        !memory_.IsA(inventory, u"DishonoredInventory")) return false;
    GameArray items{};
    // 00bd1520 uses m_AbstractItem (+c8), 12-byte records: object, quantity,
    // flags. It excludes hidden records and grouped child definitions.
    if (!memory_.Array(inventory + 0xc8, items, 16384, 12)) return false;
    uint64_t hash = HashSeed;
    bool complete = true;
    for (int32_t i = items.count; i > 0; --i) {
        struct Record { Address item; int32_t quantity{}; uint32_t flags{}; } record{};
        if (!memory_.Read(items.data + uint32_t(i - 1) * 12, record)) { complete = false; break; }
        if (record.flags & 1) continue;
        Address parent{}; uint8_t section{};
        if (!memory_.IsA(record.item, u"DisAbstractItem") || !memory_.Read(record.item + 0x60, section) ||
            !memory_.Read(record.item + 0x70, parent)) { complete = false; break; }
        if (parent || section < 1 || section > 6) continue;
        // The same owned collection also contains journal/maps. Their content
        // uses native grouped-description construction (00bd1020), so they are
        // not published as complete readable documents by this initial adapter.
        auto& collection = snapshot.equipment;
        ++collection.domain.total;
        if (collection.domain.stored >= collection.entries.size()) { complete = false; continue; }
        auto& entry = collection.entries[collection.domain.stored];
        Address title{}, description{};
        if (!FieldAt(record.item, u"m_ItemName", u"StrProperty", 12, title) ||
            !FieldAt(record.item, u"m_Description", u"StrProperty", 12, description) ||
            !Text(title, snapshot, entry.label.title) || !Text(description, snapshot, entry.label.description)) {
            complete = false; break;
        }
        entry.key = Key(record.item, bridge::Page::Equipment);
        if (!entry.key) { complete = false; break; }
        entry.quantity = {record.quantity, true};
        entry.label.artwork = ItemIcon(record.item);
        switch (section) {
            case 2: entry.kind = bridge::EquipmentKind::Key; break;
            case 3: entry.kind = bridge::EquipmentKind::Resource; break;
            case 4: entry.kind = bridge::EquipmentKind::Gadget; break;
            case 5: entry.kind = bridge::EquipmentKind::Ammunition; break;
            case 6: entry.kind = bridge::EquipmentKind::Upgrade; break;
            default: entry.kind = bridge::EquipmentKind::Unknown; break;
        }
        hash = HashValue(hash, entry.key); hash = HashValue(hash, record.quantity);
        hash = HashValue(hash, section); hash = HashText(hash, snapshot, entry.label.title);
        hash = HashText(hash, snapshot, entry.label.description);
        ++collection.domain.stored;
    }
    // Live weapon slots, ammo-info records, elixirs and upgrades are separate
    // domains in the native inventory. Do not label abstract items as all inventory.
    snapshot.equipment.domain.availability = bridge::Availability::Incomplete;
    hash = HashValue(hash, complete); hash = HashValue(hash, snapshot.equipment.domain.total);
    // Private intermediate hash: Equipment finalizes the combined domain once.
    snapshot.equipment.domain.revision = hash;
    return complete;
}
void StateAdapter::Equipment(Address pawn, bridge::Snapshot& snapshot) noexcept {
    bool complete = AbstractInventory(pawn, snapshot);
    auto& collection = snapshot.equipment;
    uint64_t hash = collection.domain.revision;
    if (!Weapons(pawn, snapshot)) complete = false;
    std::array<AmmoBinding, 12> bindings{};
    if (!AmmunitionBindings(pawn, bindings)) complete = false;
    const auto equipEligibility = NativeInput(0x10), ammoEligibility = NativeInput(8);
    Address inventory{}, field{};
    const bool inventoryKnown = memory_.Read(pawn + 0x59c, inventory) && memory_.IsA(inventory, u"DishonoredInventory");
    if (inventoryKnown && definitions_) {
        struct Ammo { int32_t count, capacity; };
        static_assert(sizeof(Ammo) == 8);
        GameArray ammo{}; std::array<Ammo, 12> amounts{};
        // 00bd2000 enumerates inventory+bc by eDisAmmoType; the native
        // DisAmmoInfo schema independently identifies count/capacity order.
        if (FieldAt(inventory, u"m_AmmoInfo", u"ArrayProperty", 12, field) && field == inventory + 0xbc &&
            memory_.Array(field, ammo, amounts.size(), sizeof(Ammo)) &&
            memory_.Bytes(ammo.data, amounts.data(), uint32_t(ammo.count) * sizeof(Ammo))) {
            for (int32_t i = 0; i < ammo.count; ++i) {
                if (amounts[i].count < 0 || amounts[i].capacity < 0) { complete = false; continue; }
                // Retain a known zero for an owned weapon's unlocked ammo
                // type; reserve quantities alone also establish acquisition.
                if (!amounts[i].count && !bindings[i].item) continue;
                ++collection.domain.total;
                if (collection.domain.stored >= collection.entries.size()) { complete = false; continue; }
                auto name = definitions_->AmmoName(uint8_t(i));
                if (name.empty()) { complete = false; continue; }
                auto& entry = collection.entries[collection.domain.stored];
                if (!Literal(name, snapshot, entry.label.title) ||
                    !Literal(definitions_->AmmoDescription(uint8_t(i)), snapshot, entry.label.description)) { complete = false; break; }
                entry.key = (uint64_t(0x300) << 32) | (uint64_t(i) + 1);
                entry.kind = bridge::EquipmentKind::Ammunition;
                entry.weapon = bindings[i].key;
                constexpr std::u16string_view icons[] = {u"RegularBullets", u"ExplosiveBullets", u"CrossbowRegularBolt",
                    u"CrossbowSleepDart", u"CrossbowFlareBolt", u"SpringRazor", u"CorvoGrenade", u"CorvoStickyGrenade"};
                if (i < 8) entry.label.artwork = {ItemArtwork(icons[i])};
                entry.quantity = {amounts[i].count, true}; entry.capacity = {amounts[i].capacity, true};
                entry.selected = {bindings[i].selected && bindings[i].current == i, bindings[i].known};
                entry.selectable = {nativeActions_ && bindings[i].item && amounts[i].count > 0 &&
                    equipEligibility.value && ammoEligibility.value, equipEligibility.known && ammoEligibility.known};
                ++collection.domain.stored;
            }
        } else complete = false;
        int32_t elixirs[2]{};
        Address elixirTweaks{};
        const bool elixirTweaksKnown = PlayerTweaks(pawn, elixirTweaks);
        const auto consumableEligibility = NativeInput(0x40);
        if (FieldAt(inventory, u"m_ElixirCounts", u"IntProperty", sizeof(elixirs), field) && field == inventory + 0xd4 &&
            memory_.Bytes(field, elixirs, sizeof(elixirs))) {
            for (uint8_t i = 0; i < 2; ++i) {
                if (elixirs[i] < 0) { complete = false; continue; }
                if (!elixirs[i]) continue;
                ++collection.domain.total;
                if (collection.domain.stored >= collection.entries.size()) { complete = false; continue; }
                auto name = definitions_->ConsumableName(i);
                if (name.empty()) { complete = false; continue; }
                auto& entry = collection.entries[collection.domain.stored];
                if (!Literal(name, snapshot, entry.label.title) ||
                    !Literal(definitions_->ConsumableDescription(i), snapshot, entry.label.description)) { complete = false; break; }
                entry.key = (uint64_t(0x301) << 32) | (uint64_t(i) + 1);
                entry.kind = bridge::EquipmentKind::Consumable; entry.quantity = {elixirs[i], true};
                entry.label.artwork = {ItemArtwork(i ? u"ManaElixir" : u"HealthElixir")};
                const auto current = i == 0 ? snapshot.health : snapshot.mana;
                const auto maximum = i == 0 ? snapshot.maximumHealth : snapshot.maximumMana;
                entry.selectable = {nativeActions_ && consumableEligibility.value && current.value < maximum.value,
                    consumableEligibility.known && current.known && maximum.known};
                // Native add/set 00c04a60/00c04b10 clamp directly to these
                // two current player-tweak fields; no capacity attribute is
                // consulted. Potency attributes affect restoration instead.
                if (elixirTweaksKnown && FieldAt(elixirTweaks, i ? u"m_nMaxManaElixir" : u"m_nMaxHealthElixir",
                    u"IntProperty", 4, field) && field == elixirTweaks + (i ? 0x204 : 0x1fc)) {
                    entry.capacity.known = memory_.Read(field, entry.capacity.value) && entry.capacity.value >= 0;
                }
                ++collection.domain.stored;
            }
        } else complete = false;
    } else complete = false;
    GameArray upgrades{}; std::array<Address, 512> owned{}, parents{};
    if (memory_.Array(pawn + 0xb50, upgrades, owned.size(), 4) &&
        memory_.Bytes(upgrades.data, owned.data(), uint32_t(upgrades.count) * 4)) {
        bool valid = true;
        for (int32_t i = 0; i < upgrades.count; ++i) {
            if (!memory_.IsA(owned[i], u"DisTweaks_Upgrade") ||
                !FieldAt(owned[i], u"m_pParentUpgrade", u"ObjectProperty", 4, field) || field != owned[i] + 0xe0 ||
                !memory_.Read(field, parents[i])) { valid = false; break; }
        }
        // Native 00bd2280 hides a purchased predecessor when another owned
        // upgrade points to it as parent. Never enumerate the whole store.
        for (int32_t i = upgrades.count; valid && i > 0; --i) {
            Address upgrade = owned[i - 1]; uint8_t section{};
            if (!memory_.Read(upgrade + 0xd0, section)) { complete = false; break; }
            if (section != 6 || std::find(parents.begin(), parents.begin() + upgrades.count, upgrade) != parents.begin() + upgrades.count) continue;
            ++collection.domain.total;
            if (collection.domain.stored >= collection.entries.size()) { complete = false; continue; }
            auto& entry = collection.entries[collection.domain.stored];
            Address title{}, description{};
            if (!FieldAt(upgrade, u"m_Name", u"StrProperty", 12, title) || title != upgrade + 0xb8 ||
                !FieldAt(upgrade, u"m_Description", u"StrProperty", 12, description) || description != upgrade + 0xc4 ||
                !Text(title, snapshot, entry.label.title) || !Text(description, snapshot, entry.label.description)) { complete = false; break; }
            entry.key = Key(upgrade, bridge::Page::Equipment);
            if (!entry.key) { complete = false; break; }
            entry.kind = bridge::EquipmentKind::Upgrade; entry.quantity = {1, true};
            entry.label.artwork = ItemIcon(upgrade);
            entry.selectable = {false, true}; ++collection.domain.stored;
        }
        if (!valid) complete = false;
    } else complete = false;
    keyRingComplete_ = KeyRing(pawn, snapshot);
    if (!keyRingComplete_) complete = false;
    for (uint32_t i = 0; i < collection.domain.stored; ++i) {
        const auto& entry = collection.entries[i];
        hash = HashValue(hash, entry.key); hash = HashValue(hash, entry.kind);
        hash = HashValue(hash, entry.label.artwork.id);
        hash = HashValue(hash, entry.quantity.known); hash = HashValue(hash, entry.quantity.value);
        hash = HashValue(hash, entry.capacity.known); hash = HashValue(hash, entry.capacity.value);
        hash = HashValue(hash, entry.weapon);
        hash = HashValue(hash, entry.damage.known); hash = HashValue(hash, entry.damage.value);
        hash = HashValue(hash, entry.headshotMultiplier.known); hash = HashValue(hash, entry.headshotMultiplier.value);
        hash = HashValue(hash, entry.rangedDamage);
        hash = HashValue(hash, entry.selected.known); hash = HashValue(hash, entry.selected.value);
        hash = HashValue(hash, entry.selectable.known); hash = HashValue(hash, entry.selectable.value);
        hash = HashText(hash, snapshot, entry.label.title); hash = HashText(hash, snapshot, entry.label.description);
    }
    collection.domain.availability = bridge::Availability::Incomplete;
    hash = HashValue(hash, complete); hash = HashValue(hash, collection.domain.total);
    collection.domain.revision = Revision(uint32_t(bridge::Page::Equipment), hash);
}

bool StateAdapter::KeyRing(Address pawn, bridge::Snapshot& snapshot) noexcept {
    Address ring{}, field{}; GameArray keys{};
    if (!FieldAt(pawn, u"m_pKeyRing", u"ObjectProperty", 4, field) || field != pawn + 0xa90 ||
        !memory_.Read(field, ring) || !memory_.IsA(ring, u"DisKeyRing") ||
        !FieldAt(ring, u"m_Keys", u"ArrayProperty", 12, field) || field != ring + 0x38 ||
        !memory_.Array(field, keys, 4096, 0x1c)) return false;
    auto& collection = snapshot.equipment;
    const uint32_t first = collection.domain.stored;
    bool complete = true;
    // Journal 00bd1920 visits owned key records backwards and merges exact
    // localized-name matches. +0 is the internal key ID, not readable text;
    // +c is optional DisTweaks_Key and +10 is the localized name FString.
    for (int32_t i = keys.count; i > 0; --i) {
        const Address record = keys.data + uint32_t(i - 1) * 0x1c;
        uint32_t length{};
        if (!memory_.String(record + 0x10, stringBuffer_.get(), MaximumString, length)) return false;
        const std::u16string_view name(stringBuffer_.get(), length);
        bool duplicate = false;
        for (uint32_t j = first; j < collection.domain.stored; ++j) {
            auto& existing = collection.entries[j];
            if (bridge::ValidText(snapshot, existing.label.title) &&
                std::u16string_view(snapshot.text.data() + existing.label.title.offset, existing.label.title.length) == name) {
                ++existing.quantity.value; duplicate = true; break;
            }
        }
        if (duplicate) continue;
        ++collection.domain.total;
        if (collection.domain.stored >= collection.entries.size()) { complete = false; continue; }
        auto& entry = collection.entries[collection.domain.stored];
        if (!Literal(name, snapshot, entry.label.title)) return false;
        entry.key = KeyRingKeyPrefix | uint32_t(i);
        entry.kind = bridge::EquipmentKind::Key; entry.quantity = {1, true}; entry.selectable = {false, true};
        Address tweaks{};
        if (!memory_.Read(record + 0xc, tweaks)) return false;
        if (tweaks) {
            if (!memory_.IsA(tweaks, u"DisTweaks_Key") ||
                !FieldAt(tweaks, u"m_Description", u"StrProperty", 12, field) || field != tweaks + 0xdc ||
                !Text(field, snapshot, entry.label.description)) { complete = false; continue; }
            entry.label.artwork = ItemIcon(tweaks);
        }
        ++collection.domain.stored;
    }
    return complete;
}
bool StateAdapter::SelectedItem(Address inventory, uint8_t usage, Address& item) noexcept {
    item = 0; uint8_t flags{}; int32_t index{}; GameArray slots{};
    if (usage < 1 || usage > 2 || !memory_.Read(inventory + 0xe0, flags)) return false;
    if (!(flags & 1)) return true;
    if (!memory_.Read(inventory + 0x44 + uint32_t(usage) * 0x28, index)) return false;
    if (index == -1) return true;
    if (!memory_.Array(inventory + 0x38, slots, 256, 12) || index < 0 || index >= slots.count ||
        !memory_.Read(slots.data + uint32_t(index) * 12, item)) return false;
    return !item || memory_.IsA(item, u"DishonoredInventoryItem");
}
bool StateAdapter::WeaponDefinition(Address item, Address& tweaks, Address& player) noexcept {
    // Concrete tweak references are declared in the installed cooked schema.
    // Native 00bd1e40 uses the item's tweak interface then +104; resolve the
    // matching object property without calling the interface's default-object
    // construction branch. Derived DLC classes inherit these properties.
    struct Definition { std::u16string_view type, field; };
    constexpr Definition definitions[] = {
        {u"DishonoredWepPistol", u"m_pPistolTweaks"}, {u"DishonoredWepSword", u"m_pSwordTweaks"},
        {u"DisWepCrossbow", u"m_pCrossbowTweaks"}, {u"DisWepGrenade", u"m_pGrenadeTweaks"},
        {u"DisGadget_SpringRazor", u"m_pSpringRazorTweaks"}, {u"DisGadget_Heart", u"m_pHeartTweaks"},
        {u"DisWepThrowingHand", u"m_pThrowingHandTweaks"},
    };
    tweaks = player = 0; Address field{};
    for (const auto& definition : definitions) {
        if (!memory_.IsA(item, definition.type)) continue;
        return FieldAt(item, definition.field, u"ObjectProperty", 4, field) && memory_.Read(field, tweaks) &&
            memory_.IsA(tweaks, u"DisTweaks_InventoryItem") &&
            FieldAt(tweaks, u"m_pPlayerSpecificTweaks", u"ObjectProperty", 4, field) && field == tweaks + 0x104 &&
            memory_.Read(field, player) && memory_.IsA(player, u"DisTweaks_InventoryItem_PlayerSpecific");
    }
    return false;
}
void StateAdapter::WeaponAttributes(Address item, bridge::Equipment& entry) noexcept {
    const bool ranged = memory_.IsA(item, u"DishonoredWeapon_Ranged");
    if (!ranged && !memory_.IsA(item, u"DishonoredWepSword")) return;
    Address field{}, attributes{}, map{};
    // Native interface item+3c, vtable11630d4 slot8 -> bfea80: this+a8
    // resolves item+e4. c86410 then calls the modified-attribute getter.
    // Resolve the same reflected live object without mutating its cache.
    if (!FieldAt(item, u"m_pAttributes", u"ObjectProperty", 4, field) || field != item + 0xe4 ||
        !memory_.Read(field, attributes) || !memory_.IsA(attributes, u"DisAttributes") ||
        !FieldAt(attributes, u"m_ModifiedAttributes", u"MapProperty", 0x3c, map) || map != attributes + 0x38) return;
    auto read = [&](Address nameRva, std::u16string_view expected, bridge::Number& result) {
        GameName name{}; char16_t text[64]{}; uint32_t length{}; float value{};
        if (!memory_.Read(memory_.Image() + nameRva, name) || !memory_.Name(name, text, 64, length) ||
            std::u16string_view(text, length) != expected || !memory_.CachedAttribute(map, name, value)) return;
        result = {value, true};
    };
    entry.rangedDamage = ranged;
    // 009e733c/009e70d8 initialize these exact native FNames. Melee attack
    // c078c0 adds a pawn bonus and context multiplier later; ranged contexts
    // c252b6/c29cdf likewise pass the weapon value into target damage handling.
    read(ranged ? 0x10518dc : 0x105184c, ranged ? u"Attribute_RangedDamage" : u"Attribute_MeleeDamage", entry.damage);
    if (ranged) read(0x10518e4, u"Attribute_RangedHeadshotMultiplier", entry.headshotMultiplier);
}
bool StateAdapter::Weapons(Address pawn, bridge::Snapshot& snapshot) noexcept {
    Address inventory{}, field{}; GameArray slots{};
    struct Slot { Address item, requiredClass; uint8_t usage, padding[3]; };
    static_assert(sizeof(Slot) == 12);
    std::array<Slot, 256> records{};
    if (!memory_.Read(pawn + 0x59c, inventory) || !memory_.IsA(inventory, u"DishonoredInventory") ||
        !memory_.Array(inventory + 0x38, slots, records.size(), sizeof(Slot)) ||
        !memory_.Bytes(slots.data, records.data(), uint32_t(slots.count) * sizeof(Slot))) return false;
    Address selected[2]{};
    const bool selectedKnown[2] = {SelectedItem(inventory, 1, selected[0]), SelectedItem(inventory, 2, selected[1])};
    const auto eligibility = NativeInput(0x10);
    auto& collection = snapshot.equipment; bool complete = true;
    for (int32_t i = 0; i < slots.count; ++i) {
        const auto record = records[i];
        if (!record.item) continue;
        bool duplicate = false;
        for (int32_t j = 0; j < i; ++j) if (records[j].item == record.item) { duplicate = true; break; }
        if (duplicate) continue;
        if (!memory_.IsA(record.item, u"DishonoredInventoryItem")) { complete = false; continue; }
        if (memory_.IsA(record.item, u"DisItemPowers") || memory_.IsA(record.item, u"DishonoredItemEmpty")) continue;
        Address tweaks{}, player{}, owner{};
        if (!FieldAt(record.item, u"m_pOwningInventory", u"ObjectProperty", 4, field) ||
            !memory_.Read(field, owner) || owner != inventory || !WeaponDefinition(record.item, tweaks, player)) {
            complete = false; continue;
        }
        ++collection.domain.total;
        if (collection.domain.stored >= collection.entries.size()) { complete = false; continue; }
        auto& entry = collection.entries[collection.domain.stored];
        Address title{}, description{};
        if (!FieldAt(player, u"m_Name", u"StrProperty", 12, title) || title != player + 0x14c ||
            !FieldAt(player, u"m_Description", u"StrProperty", 12, description) || description != player + 0x158 ||
            !Text(title, snapshot, entry.label.title) || !entry.label.title.length ||
            !Text(description, snapshot, entry.label.description)) { complete = false; continue; }
        entry.key = Key(record.item, bridge::Page::Equipment);
        if (!entry.key) { complete = false; continue; }
        entry.kind = memory_.IsA(record.item, u"DisGadget_Heart") || memory_.IsA(record.item, u"DisGadget_SpringRazor") ?
            bridge::EquipmentKind::Gadget : bridge::EquipmentKind::Weapon;
        entry.quantity = {1, true};
        entry.label.artwork = ItemIcon(player);
        entry.selected = {selected[0] == record.item || selected[1] == record.item, selectedKnown[0] && selectedKnown[1]};
        const bool usageKnown = record.usage == 1 || record.usage == 2;
        entry.selectable = {nativeActions_ && eligibility.value && usageKnown, eligibility.known};
        WeaponAttributes(record.item, entry);
        ++collection.domain.stored;
    }
    return complete;
}
bool StateAdapter::AmmunitionBindings(Address pawn, std::array<AmmoBinding, 12>& bindings) noexcept {
    bindings = {};
    Address inventory{}, field{}; GameArray slots{};
    if (!memory_.Read(pawn + 0x59c, inventory) || !memory_.IsA(inventory, u"DishonoredInventory") ||
        !memory_.Array(inventory + 0x38, slots, 256, 12)) return false;
    Address selected[2]{};
    const bool selectedKnown = SelectedItem(inventory, 1, selected[0]) && SelectedItem(inventory, 2, selected[1]);
    bool complete = selectedKnown;
    for (int32_t slotIndex = 0; slotIndex < slots.count; ++slotIndex) {
        struct Slot { Address item, type; uint8_t usage{}, padding[3]{}; } slot{};
        if (!memory_.Read(slots.data + uint32_t(slotIndex) * 12, slot)) return false;
        if (!slot.item || (slot.usage != 1 && slot.usage != 2)) continue;
        if (!memory_.IsA(slot.item, u"DishonoredWeapon_Ranged")) continue;
        Address owner{}, tweaks{}, player{}; uint8_t current{}; GameArray types{};
        if (!FieldAt(slot.item, u"m_pOwningInventory", u"ObjectProperty", 4, field) ||
            !memory_.Read(field, owner) || owner != inventory || !WeaponDefinition(slot.item, tweaks, player) ||
            !memory_.IsA(tweaks, u"DisTweaks_WeaponRanged") ||
            !FieldAt(tweaks, u"m_RangedAmmoTypes", u"ArrayProperty", 12, field) || field != tweaks + 0x10c ||
            !memory_.Array(field, types, 12, 8) || !memory_.Read(slot.item + 0x118, current) || current > 12) {
            complete = false; continue;
        }
        const bool equipped = selected[0] == slot.item || selected[1] == slot.item;
        for (int32_t i = 0; i < types.count; ++i) {
            uint8_t type{};
            if (!memory_.Read(types.data + uint32_t(i) * 8, type) || type >= bindings.size()) { complete = false; continue; }
            if (type == 1 || type == 7) {
                // Native NextAmmoType 00c2bcc0 checks item attributes through
                // interface +3c; its getter 00bfea80 returns item+e4.
                Address attributes{}, map{}; GameName name{}; float unlocked{};
                if (!FieldAt(slot.item, u"m_pAttributes", u"ObjectProperty", 4, field) || field != slot.item + 0xe4 ||
                    !memory_.Read(field, attributes) || !memory_.IsA(attributes, u"DisAttributes") ||
                    !FieldAt(attributes, u"m_ModifiedAttributes", u"MapProperty", 0x3c, map) || map != attributes + 0x38 ||
                    !memory_.Read(memory_.Image() + (type == 1 ? 0x10516bc : 0x105195c), name) ||
                    !memory_.CachedAttribute(map, name, unlocked)) { complete = false; continue; }
                if (unlocked == 0) continue;
            }
            auto& binding = bindings[type];
            if (!binding.item || (!binding.selected && equipped))
                binding = {slot.item, Key(slot.item, bridge::Page::Equipment), slot.usage, current, equipped, selectedKnown};
        }
    }
    return complete;
}
bool StateAdapter::JournalPart(Address item, bridge::Snapshot& snapshot, bool& anyText) noexcept {
    Address vtable{}, getter{}, field{}; uint32_t length{};
    // DisAbstractItem registration 01368100 -> constructor 00c4e7a0 ->
    // vtable 010eb5d0, slot124 -> 00a4dc00. This getter copies FString+50.
    // A subclass override is not assumed equivalent and is never invoked.
    if (!memory_.Read(item, vtable) || !Memory::Span(vtable, 0x128) ||
        !memory_.Read(vtable + 0x124, getter) || getter != memory_.Image() + 0x64dc00 ||
        !FieldAt(item, u"m_Description", u"StrProperty", 12, field) || field != item + 0x50 ||
        !memory_.String(field, stringBuffer_.get(), MaximumString, length)) return false;
    if (!length) return true;
    const uint32_t separator = anyText ? 2 : 0;
    if (snapshot.textUsed > snapshot.text.size() || length + separator + 1 > snapshot.text.size() - snapshot.textUsed) return false;
    // Native 00bd1020 produces separate strings/pages. Preserve all their
    // text in order, separated by a paragraph in the continuous touch reader.
    if (separator) { snapshot.text[snapshot.textUsed++] = u'\n'; snapshot.text[snapshot.textUsed++] = u'\n'; }
    std::copy_n(stringBuffer_.get(), length, snapshot.text.data() + snapshot.textUsed);
    snapshot.textUsed += length; anyText = true;
    return true;
}
void StateAdapter::Journal(Address pawn, bridge::Snapshot& snapshot) noexcept {
    Address inventory{}; GameArray items{};
    auto& work = *journal_;
    work.valid = false; work.recordCount = 0; work.entryCount = 0;
    static_assert(sizeof(JournalWorkspace::Record) == 12);
    if (!memory_.Read(pawn + 0x59c, inventory) || !memory_.IsA(inventory, u"DishonoredInventory") ||
        !memory_.Array(inventory + 0xc8, items, work.records.size(), sizeof(JournalWorkspace::Record)) ||
        !memory_.Bytes(items.data, work.records.data(), uint32_t(items.count) * sizeof(JournalWorkspace::Record))) return;
    work.groups.fill({}); work.heads.fill(-1); work.tails.fill(-1);
    std::fill_n(work.next.begin(), items.count, -1);
    // Validate/cache parent links once. Grouping below is bounded O(n), rather
    // than rescan the entire inventory separately for each readable document.
    for (int32_t i = 0; i < items.count; ++i) {
        if (!memory_.IsA(work.records[i].item, u"DisAbstractItem") ||
            !memory_.Read(work.records[i].item + 0x70, work.parents[i])) return;
    }
    auto group = [&work](Address item) noexcept -> JournalWorkspace::Group* {
        uint32_t slot = ((item >> 4) * uint32_t(2654435761)) >> 21;
        for (uint32_t probe = 0; probe < 64; ++probe) {
            auto& candidate = work.groups[(slot + probe) & 2047];
            if (!candidate.item || candidate.item == item) return &candidate;
        }
        return nullptr;
    };
    auto& collection = snapshot.journal;
    bool complete = true;
    for (int32_t i = items.count; i > 0; --i) {
        const auto& record = work.records[i - 1]; uint8_t section{};
        if ((record.flags & 1) || work.parents[i - 1]) continue;
        if (!memory_.Read(record.item + 0x60, section)) { complete = false; break; }
        if (section != 2 && section != 7 && section != 8 && section != 9 && section != 12 && section != 13 && section != 14) continue;
        ++collection.domain.total;
        if (collection.domain.stored >= collection.entries.size()) { complete = false; continue; }
        auto* target = group(record.item);
        if (!target || target->item) { complete = false; continue; }
        auto& entry = collection.entries[collection.domain.stored];
        Address title{};
        if (!FieldAt(record.item, u"m_ItemName", u"StrProperty", 12, title) || !Text(title, snapshot, entry.label.title)) { complete = false; break; }
        entry.key = Key(record.item, bridge::Page::Journal);
        if (!entry.key) { complete = false; break; }
        entry.label.artwork = ItemIcon(record.item);
        entry.kind = section == 2 ? bridge::JournalKind::Key : section == 7 ? bridge::JournalKind::Audiograph : section == 9 ? bridge::JournalKind::MissionDocument :
            section == 12 ? bridge::JournalKind::Book : section >= 13 ? bridge::JournalKind::Clue : bridge::JournalKind::Note;
        if (entry.kind == bridge::JournalKind::Audiograph) AudioLogState(record.item, entry);
        if (section == 8) {
            // 00bd1520 distinguishes book/sheet definitions by the original
            // icon name. This is presentation only, never an unlock predicate.
            uint32_t length{};
            if (!memory_.String(record.item + 0x64, stringBuffer_.get(), MaximumString, length)) { complete = false; break; }
            if (std::u16string_view(stringBuffer_.get(), length).find(u"ReadableNote_Book") != std::u16string_view::npos)
                entry.kind = bridge::JournalKind::Book;
        }
        work.roots[collection.domain.stored] = i - 1;
        *target = {record.item, collection.domain.stored};
        ++collection.domain.stored;
    }
    for (int32_t i = 0; i < items.count; ++i) {
        if (!work.parents[i]) continue;
        auto* target = group(work.parents[i]);
        if (!target) { complete = false; continue; }
        if (!target->item) continue;
        // Native 00bd1020 includes owned children even if their inventory-row
        // hidden flag is set. It does not enumerate unowned definitions.
        auto& tail = work.tails[target->entry];
        if (tail < 0) work.heads[target->entry] = i;
        else work.next[tail] = i;
        tail = i;
    }
    work.recordCount = items.count; work.entryCount = collection.domain.stored; work.valid = true;
    uint32_t priority = collection.domain.stored;
    if (document_->key && document_->generation == generation_)
        for (uint32_t i = 0; i < collection.domain.stored; ++i) if (collection.entries[i].key == document_->key) { priority = i; break; }
    // Reserve document space for the explicitly requested reader first. A
    // large earlier inventory entry must not starve the selected document.
    for (uint32_t pass = 0; pass < collection.domain.stored; ++pass) {
        const uint32_t i = priority == collection.domain.stored ? pass : pass == 0 ? priority : pass <= priority ? pass - 1 : pass;
        auto& entry = collection.entries[i];
        const uint32_t start = snapshot.textUsed;
        bool anyText = false;
        bool readable = false;
        if (document_->key == entry.key && document_->generation == generation_ &&
            document_->signature == JournalGroupSignature(i) && document_->length < snapshot.text.size() - snapshot.textUsed) {
            std::copy_n(document_->text.data(), document_->length, snapshot.text.data() + snapshot.textUsed);
            snapshot.textUsed += document_->length; readable = true;
        } else {
            readable = JournalPart(work.records[work.roots[i]].item, snapshot, anyText);
            for (int32_t child = work.heads[i]; readable && child >= 0; child = work.next[child])
                readable = JournalPart(work.records[child].item, snapshot, anyText);
        }
        if (readable && snapshot.textUsed < snapshot.text.size()) {
            entry.contents = {start, snapshot.textUsed - start};
            snapshot.text[snapshot.textUsed++] = 0;
            entry.contentsComplete = true;
        } else {
            // No partial document is presented as complete; restore the arena
            // so a large/unsupported document cannot consume later entries.
            snapshot.textUsed = start;
            if (start < snapshot.text.size()) snapshot.text[start] = 0;
            complete = false;
        }
    }
    // Append only after document grouping/copying: these entries are native
    // key-ring records, not DisAbstractItem roots eligible for a native getter.
    if (!AppendKeyRingJournal(snapshot)) complete = false;
    uint64_t hash = HashSeed;
    for (uint32_t i = 0; i < collection.domain.stored; ++i) {
        const auto& entry = collection.entries[i];
        hash = HashValue(hash, entry.key); hash = HashValue(hash, entry.kind);
        hash = HashText(hash, snapshot, entry.label.title); hash = HashValue(hash, entry.contentsComplete);
        hash = HashValue(hash, entry.playing.known); hash = HashValue(hash, entry.playing.value);
        hash = HashValue(hash, entry.canPlay.known); hash = HashValue(hash, entry.canPlay.value);
        hash = HashValue(hash, entry.canStop.known); hash = HashValue(hash, entry.canStop.value);
        if (entry.contentsComplete) hash = HashText(hash, snapshot, entry.contents);
    }
    // An unread native transcript, failed key-ring read, or truncated document
    // keeps this partial. A fully read empty collection is legitimately empty,
    // rather than an unavailable journal (the same applies to zero charms).
    collection.domain.availability = complete && keyRingComplete_ && !memory_.Failed() ?
        bridge::Availability::Available : bridge::Availability::Incomplete;
    hash = HashValue(hash, collection.domain.availability);
    hash = HashValue(hash, complete); hash = HashValue(hash, collection.domain.total);
    collection.domain.revision = Revision(uint32_t(bridge::Page::Journal), hash);
}
bool StateAdapter::GameInfo(Address& game) noexcept {
    game = 0;
    Address world{}, info{};
    return memory_.Read(memory_.Image() + 0x103d878, world) && memory_.IsA(world, u"World") &&
        memory_.Read(world + 0x2c0, info) && memory_.IsA(info, u"WorldInfo") &&
        memory_.Read(info + 0x410, game) && memory_.IsA(game, u"DishonoredGameInfo");
}
void StateAdapter::Chaos(Address pawn, bridge::Snapshot& snapshot) noexcept {
    Address manager{}, field{}, tweaks{}, game{}; int32_t score{}, mission{}, threshold{};
    GameArray levels{}; std::array<int32_t, 64> thresholds{};
    // Native C47FF0 walks the darkness thresholds backwards, returning the
    // last matching index (or -1). A01EB0/A07B40 compare that index with the
    // current mission's threshold; raw mission statistic 26 is not this input.
    if (!FieldAt(pawn, u"m_pDarknessManager", u"ObjectProperty", 4, field) || field != pawn + 0xa94 ||
        !memory_.Read(field, manager) || !memory_.IsA(manager, u"DisDarknessManager") ||
        !FieldAt(manager, u"m_DarknessScore", u"IntProperty", 4, field) || field != manager + 0x38 ||
        !memory_.Read(field, score) || !PlayerTweaks(pawn, tweaks) ||
        !FieldAt(tweaks, u"m_DarknessLevels", u"ArrayProperty", 12, field) || field != tweaks + 0x2c4 ||
        !memory_.Array(field, levels, thresholds.size(), 4) ||
        !memory_.Bytes(levels.data, thresholds.data(), uint32_t(levels.count) * 4) || !GameInfo(game) ||
        !FieldAt(game, u"m_CurrentMissionNum", u"IntProperty", 4, field) || field != game + 0x448 ||
        !memory_.Read(field, mission) || mission < 0 || mission >= 21 ||
        !FieldAt(game, u"m_MissionChaosThreshold", u"IntProperty", 21 * 4, field) || field != game + 0x15dc ||
        !memory_.Read(field + uint32_t(mission) * 4, threshold)) return;
    int32_t level = levels.count - 1;
    while (level >= 0 && score < thresholds[uint32_t(level)]) --level;
    snapshot.chaos = {level >= threshold ? bridge::ChaosLevel::High : bridge::ChaosLevel::Low, true};
}
bool StateAdapter::SavedStatisticsDefinitions(Address game, Address& tweaks) noexcept {
    Address manager{}, menu{}, field{};
    tweaks = 0;
    if (!FieldAt(game, u"m_pGlobalUIManager", u"ObjectProperty", 4, field) ||
        !memory_.Read(field, manager)) return false;
    if (manager) {
        if (!memory_.IsA(manager, u"DisGlobalUIManager") ||
            !FieldAt(manager, u"m_pMainMenu", u"ObjectProperty", 4, field) ||
            !memory_.Read(field, menu)) return false;
        if (menu) {
            if (!memory_.IsA(menu, u"DisGFxMoviePlayerMainMenu") ||
                !FieldAt(menu, u"m_pTweaks", u"ObjectProperty", 4, field) || field != menu + 0x1b0 ||
                !memory_.Read(field, tweaks)) return false;
            if (tweaks) return memory_.IsA(tweaks, u"DisTweaks_GFxMoviePlayerMainMenu");
        }
    }
    // Native saved-result selection 00bdacf0 first uses the menu override.
    // At 00bdad18 it falls back to class global 01462794 and GetDefaultObject.
    // Read only an existing CDO (+13c), never allocate/open a menu. A corrupt
    // nonnull override above is an error, not a reason to silently use defaults.
    Address type{}, actual{};
    return memory_.Read(memory_.Image() + 0x1062794, type) && memory_.IsA(type, u"Class") &&
        memory_.ObjectName(type, u"DisTweaks_GFxMoviePlayerMainMenu") &&
        memory_.Read(type + 0x13c, tweaks) && memory_.Object(tweaks, actual) && actual == type;
}
bool StateAdapter::SavedStatistics(bridge::Snapshot& snapshot) noexcept {
    Address player{}, store{}, profile{}, field{}, type{}, game{}, gameClass{}, baseClass{};
    // Controller AA6BF0 -> LocalPlayer+A4 -> player data object+58.
    // AA5140 accepts only ArkProfileSettings. Read the existing profile;
    // never request storage reloads or call settings getters that allocate.
    if (!FieldAt(controller_, u"Player", u"ObjectProperty", 4, field) || field != controller_ + 0x49c ||
        !memory_.Read(field, player) || !memory_.IsA(player, u"LocalPlayer") ||
        !memory_.Read(player + 0xa4, store) || !memory_.Object(store, type) ||
        !memory_.Read(store + 0x58, profile) || !memory_.IsA(profile, u"ArkProfileSettings") ||
        !GameInfo(game) || !memory_.Object(game, gameClass) ||
        !memory_.Read(memory_.Image() + 0x1051008, baseClass) || !baseClass || gameClass != baseClass) return false;
    GameArray settings{};
    if (!memory_.Array(profile + 0x44, settings, 2048, 24)) return false;
    Address data{}; int32_t bytes{};
    for (int32_t i = 0; i < settings.count; ++i) {
        const Address record = settings.data + uint32_t(i) * 24;
        int32_t id{}; uint8_t kind{};
        if (!memory_.Read(record + 4, id)) return false;
        if (id != 0x88) continue; // Native 008f3970 takes the first setting ID match.
        if (!memory_.Read(record + 8, kind) || kind != 6 || !memory_.Read(record + 12, bytes) ||
            !memory_.Read(record + 16, data)) return false;
        break;
    }
    struct Stored { uint16_t missionTag, summary; std::array<float, 16> values; };
    static_assert(sizeof(Stored) == 0x44);
    if (bytes < 0 || bytes > 64 * int32_t(sizeof(Stored)) || bytes % sizeof(Stored) ||
        (bytes && !Memory::Span(data, uint32_t(bytes)))) return false;
    if (!data || !bytes) return true;
    Address tweaks{}; GameArray definitions{};
    if (!SavedStatisticsDefinitions(game, tweaks) ||
        !FieldAt(tweaks, u"m_MissionStatsTweaks", u"ArrayProperty", 12, field) || field != tweaks + 0x13c ||
        !memory_.Array(field, definitions, 128, 4)) return false;
    auto& collection = snapshot.statistics; bool complete = true;
    // Base game uses indices 0..8. DLC offsets/templates have separate native
    // contracts and cannot be inferred from a longer profile byte array.
    for (int32_t mission = 0; mission < std::min(9, bytes / int32_t(sizeof(Stored))); ++mission) {
        Stored stored{};
        if (!memory_.Read(data + uint32_t(mission) * sizeof(Stored), stored)) return false;
        if (!(stored.missionTag & 1)) continue; // 00938210 marks a saved result valid.
        Address definition{};
        for (int32_t i = 0; i < definitions.count; ++i) {
            Address candidate{}; int32_t index{}, number{};
            if (!memory_.Read(definitions.data + uint32_t(i) * 4, candidate)) return false;
            if (!candidate) continue;
            if (!memory_.IsA(candidate, u"DisTweaks_MissionStats") ||
                !memory_.Read(candidate + 0x8c, index) || !memory_.Read(candidate + 0x90, number)) return false;
            if (index == mission && number == (stored.missionTag >> 1)) { definition = candidate; break; }
        }
        if (!definition) { complete = false; continue; }
        GameArray stats{}, maxima{}; bridge::TextRef missionName{};
        if (!FieldAt(definition, u"m_MissionStats", u"ArrayProperty", 12, field) || field != definition + 0xa0 ||
            !memory_.Array(field, stats, 16, 0x1c) ||
            !FieldAt(definition, u"m_MissionStatsMaxValues", u"ArrayProperty", 12, field) || field != definition + 0xac ||
            !memory_.Array(field, maxima, 16, 4) ||
            !FieldAt(game, u"m_MissionNames", u"StrProperty", 21 * 12, field) || field != game + 0x14e0 ||
            !Text(field + uint32_t(mission) * 12, snapshot, missionName)) { complete = false; continue; }
        for (int32_t i = 0; i < stats.count; ++i) {
            const Address stat = stats.data + uint32_t(i) * 0x1c;
            uint8_t id{}; uint32_t flags{}; int32_t maximum{};
            const float value = stored.values[uint32_t(i)];
            if (!std::isfinite(value) || !memory_.Read(stat + 0xc, id) || !memory_.Read(stat + 0x18, flags) ||
                (i < maxima.count && !memory_.Read(maxima.data + uint32_t(i) * 4, maximum))) { complete = false; continue; }
            ++collection.domain.total;
            if (collection.domain.stored >= collection.entries.size()) { complete = false; continue; }
            auto& entry = collection.entries[collection.domain.stored];
            if (!Text(stat, snapshot, entry.label.title)) { complete = false; continue; }
            entry.key = (uint64_t(0x500 + mission) << 32) | uint32_t(i + 1);
            entry.scope = bridge::StatisticScope::SavedMission; entry.context = missionName;
            entry.value = {value, true}; entry.maximum = {float(maximum), i < maxima.count && maximum > 0};
            if (id == 27 && (value == 0 || value == 1)) entry.format = bridge::StatisticFormat::Chaos;
            else if ((flags & 1) && std::trunc(value) == value) {
                // 00BCBDC0 compares (integerValue != 0) with bCheckIfNonZero.
                entry.format = bridge::StatisticFormat::Check; entry.condition = {(value != 0) == ((flags & 2) != 0), true};
            }
            ++collection.domain.stored;
        }
        GameArray summaries{};
        if (!FieldAt(definition, u"m_MissionSummaryItems", u"ArrayProperty", 12, field) || field != definition + 0x94 ||
            !memory_.Array(field, summaries, 16, 0x24)) { complete = false; continue; }
        for (int32_t i = 0; i < summaries.count; ++i) {
            const Address summary = summaries.data + uint32_t(i) * 0x24; uint32_t flags{};
            if (!memory_.Read(summary + 0x20, flags)) { complete = false; continue; }
            // 00bd3be0 exposes narrative text only when the saved summary bit
            // equals the definition's desired story-flag value. An unmatched
            // definition is not an unlocked fact and must remain concealed.
            if (((stored.summary >> uint32_t(i)) & 1) != (flags & 1)) continue;
            ++collection.domain.total;
            if (collection.domain.stored >= collection.entries.size()) { complete = false; continue; }
            auto& entry = collection.entries[collection.domain.stored];
            if (!Text(summary, snapshot, entry.label.title)) { complete = false; continue; }
            entry.key = (uint64_t(0x500 + mission) << 32) | uint32_t(0x100 + i);
            entry.scope = bridge::StatisticScope::SavedMission; entry.context = missionName;
            entry.format = bridge::StatisticFormat::Check; entry.condition = {true, true};
            ++collection.domain.stored;
        }
    }
    return complete;
}
void StateAdapter::MissionStatistics(Address pawn, bridge::Snapshot& snapshot) noexcept {
    Chaos(pawn, snapshot);
    // 00aa93c0 reads these exact fields, and 00ab83a0 serializes this array
    // beside m_StatValues. Do not call OnGetPlayerStat: its optional flag resets
    // the stored float. Do not confuse an enum ID with m_StatValues' indices.
    struct Record { uint8_t id, padding[3]; float value; int32_t maximum; };
    static_assert(sizeof(Record) == 12);
    struct Definition { uint8_t id{}; std::u16string_view label; };
    // IDs come from DisTweaks_PlayerStats.EDisPlayerStat in the installed
    // package. These are display labels only; all values/maxima are live reads.
    constexpr Definition definitions[] = {
        {0, u"Kills"}, {1, u"Assassinations"}, {3, u"Alarms rung"},
        {4, u"Dead or unconscious bodies found"}, {10, u"NPCs alerted"},
        {12, u"Unaware kills"}, {14, u"Drop assassinations"},
        {20, u"Items collected"}, {26, u"Chaos score"},
        {28, u"Hostiles killed"}, {29, u"Hostiles put to sleep"},
        {30, u"Civilians killed"}, {31, u"Civilians put to sleep"},
        {32, u"NPCs escaped from"}, {33, u"Coins found"}, {34, u"Runes found"},
        {35, u"Bone charms found"}, {36, u"Outsider shrines found"}, {37, u"Sokolov paintings found"},
    };
    Address field{};
    if (!FieldAt(pawn, u"m_MissionStatValues", u"ArrayProperty", 12, field) || field != pawn + 0xb7c) return;
    GameArray array{}; std::array<Record, 128> records{};
    if (!memory_.Array(field, array, records.size(), sizeof(Record)) ||
        !memory_.Bytes(array.data, records.data(), uint32_t(array.count) * sizeof(Record))) return;
    uint64_t hash = HashValue(HashValue(HashSeed, snapshot.chaos.known), snapshot.chaos.value);
    auto& collection = snapshot.statistics;
    bool complete = true;
    for (const auto& definition : definitions) {
        const Record* found = nullptr;
        for (int32_t i = 0; i < array.count; ++i) if (records[i].id == definition.id) {
            // A duplicate ID would make the getter's first-match order matter.
            // Treat that as an unresolved record instead of adding totals.
            if (found) { complete = false; found = nullptr; break; }
            found = &records[i];
        }
        if (!found) continue; // Missing is unknown, not zero.
        if (!std::isfinite(found->value)) { complete = false; continue; }
        auto& entry = collection.entries[collection.domain.stored];
        if (!Literal(definition.label, snapshot, entry.label.title)) { complete = false; break; }
        entry.key = ((uint64_t(bridge::Page::Statistics) + 1) << 32) | (uint64_t(definition.id) + 1);
        entry.scope = bridge::StatisticScope::CurrentMission;
        entry.value = {found->value, true}; entry.maximum = {float(found->maximum), true};
        hash = HashValue(hash, found->id); hash = HashValue(hash, found->value); hash = HashValue(hash, found->maximum);
        ++collection.domain.stored;
    }
    collection.domain.total = collection.domain.stored;
    if (!SavedStatistics(snapshot)) complete = false;
    for (uint32_t i = 0; i < collection.domain.stored; ++i) {
        const auto& entry = collection.entries[i];
        hash = HashValue(hash, entry.key); hash = HashValue(hash, entry.format);
        hash = HashValue(hash, entry.scope);
        hash = HashValue(hash, entry.value.known); hash = HashValue(hash, entry.value.value);
        hash = HashValue(hash, entry.maximum.known); hash = HashValue(hash, entry.maximum.value);
        hash = HashValue(hash, entry.condition.known); hash = HashValue(hash, entry.condition.value);
        hash = HashText(hash, snapshot, entry.context); hash = HashText(hash, snapshot, entry.label.title);
    }
    collection.domain.total = std::max(collection.domain.total, collection.domain.stored);
    // Campaign aggregates and DLC result definitions remain outside this reader.
    collection.domain.availability = bridge::Availability::Incomplete;
    hash = HashValue(hash, complete);
    collection.domain.revision = Revision(uint32_t(bridge::Page::Statistics), hash);
}
void StateAdapter::Invalidate(bridge::Lifecycle reason, uint64_t now) noexcept {
    taskMarkers_.fill({}); ownedTaskMarkerCount_ = 0; taskMarkersKnown_ = false;
    mapHints_.fill({}); mapHintCount_ = mapHintNext_ = 0;
    if (lifecycle_ != reason || pawn_ || controller_) {
        ++generation_; pawn_ = controller_ = world_ = 0; reflection_.Clear();
        lifecycle_ = reason;
        bridge_.Record({Bridge::EventKind::Lifecycle, now, generation_, uint64_t(reason)});
    }
    auto& snapshot = bridge_.snapshots.BeginWrite();
    Reset(snapshot);
    snapshot.sequence = ++sequence_; snapshot.generation = generation_;
    snapshot.sampledAt = now; snapshot.lifecycle = reason;
    Commands(snapshot, now);
    bridge_.snapshots.Publish();
}
void StateAdapter::Tick(Address controller, uint64_t now) noexcept {
    const bool gap = lastTick_ && (now < lastTick_ || now - lastTick_ > bridge::FreshnessMilliseconds);
    lastTick_ = now;
    if (!gap && now >= lastSample_ && now - lastSample_ < 100) return;
    lastSample_ = now;
    memory_.Begin(512 * 1024, 8192, true);
    Address pawn{}, world{};
    if (!Roots(controller, pawn, &world)) {
        RootDiagnostics(controller, now);
        ReadDiagnostics(now);
        Invalidate(bridge::Lifecycle::NoPlayer, now); return;
    }
    if (gap) {
        // A paused game can stop ticking without loading a different world.
        // Expire observations, but preserve view keys/scroll state when fresh
        // root validation proves the same controller, Corvo and world remain.
        taskMarkers_.fill({}); ownedTaskMarkerCount_ = 0; taskMarkersKnown_ = false;
        mapHints_.fill({}); mapHintCount_ = mapHintNext_ = 0;
    }
    if (pawn_ != pawn || controller_ != controller || world_ != world) {
        taskMarkers_.fill({}); ownedTaskMarkerCount_ = 0; taskMarkersKnown_ = false;
        mapHints_.fill({}); mapHintCount_ = mapHintNext_ = 0;
        ++generation_; reflection_.Clear(); pawn_ = pawn; controller_ = controller; world_ = world;
        lifecycle_ = bridge::Lifecycle::Live;
        bridge_.Record({Bridge::EventKind::Lifecycle, now, generation_, uint64_t(lifecycle_)});
    }
    auto& snapshot = bridge_.snapshots.BeginWrite();
    Reset(snapshot);
    snapshot.sequence = ++sequence_; snapshot.generation = generation_;
    snapshot.sampledAt = now; snapshot.lifecycle = bridge::Lifecycle::Live;
    const bool logPhases = now >= nextReadLog_;
    uint32_t previousCalls = memory_.CallsUsed(), previousBytes = memory_.BytesUsed();
    const auto phase = [&](uint32_t id) {
        if (logPhases) bridge_.Record({Bridge::EventKind::SamplePhase, now, generation_, id, snapshot.sequence,
            int32_t(memory_.CallsUsed() - previousCalls), int32_t(memory_.BytesUsed() - previousBytes),
            int32_t(memory_.LastFailure().reason), int32_t(memory_.TypeCacheHits())});
        previousCalls = memory_.CallsUsed(); previousBytes = memory_.BytesUsed();
    };
    Scalars(pawn, snapshot);
    Currencies(pawn, snapshot);
    phase(0);
    Powers(pawn, snapshot);
    phase(1);
    BoneCharms(pawn, snapshot);
    phase(2);
    Objectives(controller, snapshot);
    phase(3);
    CurrentArea(snapshot);
    phase(4);
    Equipment(pawn, snapshot);
    phase(5);
    MissionStatistics(pawn, snapshot);
    phase(6);
    Maps(pawn, snapshot, now);
    phase(7);
    Journal(pawn, snapshot);
    phase(8);
    ReadDiagnostics(now);
    if (now >= nextReadLog_) {
        bridge_.Record({Bridge::EventKind::SampleReads, now, generation_, snapshot.sequence, 0,
            int32_t(memory_.BytesUsed()), int32_t(memory_.CallsUsed()), int32_t(memory_.CacheHits()), int32_t(memory_.TypeCacheHits())});
        bridge_.Record({Bridge::EventKind::ObjectiveMarkers, now, generation_, snapshot.sequence,
            snapshot.objectiveTargets.domain.revision, int32_t(markerObservations_), int32_t(ownedTaskMarkerCount_),
            int32_t(snapshot.objectiveTargets.domain.stored), int32_t(snapshot.objectiveTargets.domain.availability)});
        markerObservations_ = 0;
        const auto& map=snapshot.map;
        const bool positionKnown=map.playerX.known && map.playerY.known &&
            std::isfinite(map.playerX.value) && std::isfinite(map.playerY.value) &&
            map.playerX.value>=0 && map.playerX.value<=1 && map.playerY.value>=0 && map.playerY.value<=1;
        // Five-second bounded telemetry, normalized coordinates in 1/100000
        // units. Flags: position=1, facing=2, approximate=4. No tick-thread I/O.
        bridge_.Record({Bridge::EventKind::MapProjection,now,generation_,map.key,snapshot.sequence,
            int32_t(positionKnown)|(map.facingRadians.known?2:0)|(map.positionApproximate?4:0),
            positionKnown?int32_t(map.playerX.value*100000):0,
            positionKnown?int32_t(map.playerY.value*100000):0,int32_t(map.sheets.domain.stored)});
        nextReadLog_ = now + 5000;
    }
    // Roots are rechecked after sampling, before any command eligibility work.
    // Give this validation its own small budget. Exhausting a domain's budget
    // is not evidence that the player was destroyed, and must not repeatedly
    // clear already-resolved schema while initialization is still progressing.
    memory_.Begin(4096, 256);
    Address after{};
    if (!Roots(controller, after) || after != pawn) {
        if (rootStage_ == RootStage::Ready) rootStage_ = RootStage::PawnLink;
        RootDiagnostics(controller, now);
        ReadDiagnostics(now);
        Invalidate(bridge::Lifecycle::NoPlayer, now); return;
    }
    Diagnostics(snapshot, now);
    Commands(snapshot, now);
    bridge_.snapshots.Publish();
}

void StateAdapter::ReadDiagnostics(uint64_t now) noexcept {
    const auto failure = memory_.LastFailure();
    if (failure.reason == Memory::Failure::None) return;
    // Different invalid addresses must not defeat throttling during streaming
    // or budget exhaustion. Report the latest failure plus suppressed samples.
    if (now < nextFailureLog_) {
        if (suppressedFailures_ < 0x7fffffffu) ++suppressedFailures_;
        return;
    }
    bridge_.Record({Bridge::EventKind::MemoryFault, now, generation_, uint64_t(failure.reason), failure.address,
        int32_t(failure.bytes), int32_t(memory_.BytesUsed()), int32_t(memory_.CallsUsed()), int32_t(suppressedFailures_)});
    suppressedFailures_ = 0; nextFailureLog_ = now + 5000;
}
void StateAdapter::RootDiagnostics(Address controller, uint64_t now) noexcept {
    if (now < nextRootLog_) return;
    bridge_.Record({Bridge::EventKind::RootValidation, now, generation_, uint64_t(rootStage_), controller,
        int32_t(memory_.LastFailure().reason), int32_t(memory_.BytesUsed()), int32_t(memory_.CallsUsed())});
    nextRootLog_ = now + 5000;
}
void StateAdapter::Diagnostics(const bridge::Snapshot& snapshot, uint64_t now) noexcept {
    const std::array<int32_t, 4> scalars = {snapshot.health.value, snapshot.mana.value, snapshot.coins.value, snapshot.runes.value};
    const uint8_t mask = uint8_t(snapshot.health.known) | uint8_t(snapshot.mana.known) << 1 |
        uint8_t(snapshot.coins.known) << 2 | uint8_t(snapshot.runes.known) << 3;
    const bool generationChanged = loggedGeneration_ != snapshot.generation;
    if (generationChanged || scalars != loggedScalars_ || mask != loggedScalarMask_) {
        bridge_.Record({Bridge::EventKind::State, now, generation_, mask, snapshot.sequence,
            scalars[0], scalars[1], scalars[2], scalars[3]});
        loggedScalars_ = scalars; loggedScalarMask_ = mask;
    }
    const bridge::Domain* domains[] = {&snapshot.map.domain, &snapshot.powers.domain, &snapshot.equipment.domain,
        &snapshot.objectives.domain, &snapshot.journal.domain, &snapshot.charms.domain, &snapshot.statistics.domain};
    for (uint32_t i = 0; i < 7; ++i) {
        const auto& domain = *domains[i];
        if (generationChanged || loggedRevisions_[i] != domain.revision) {
            bridge_.Record({Bridge::EventKind::Domain, now, generation_, i, domain.revision,
                int32_t(domain.availability), int32_t(domain.stored), int32_t(domain.total)});
            loggedRevisions_[i] = domain.revision;
        }
    }
    loggedGeneration_ = snapshot.generation;
}

} // namespace dishonored

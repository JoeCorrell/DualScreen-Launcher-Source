#include "StateAdapter.h"
#include "TargetBuild.h"
#include <algorithm>

namespace dishonored {
namespace {
template<class T, std::size_t N>
const T* Find(const bridge::Collection<T, N>& collection, bridge::Key key) noexcept {
    if (!key || collection.domain.stored > N) return nullptr;
    for (uint32_t i = 0; i < collection.domain.stored; ++i)
        if (collection.entries[i].key == key) return &collection.entries[i];
    return nullptr;
}
// Explicit unused EDX parameter gives native thiscall's ECX receiver and
// unchanged stack arguments/cleanup using MinGW's supported fastcall ABI.
using UnaryAction = void (__attribute__((fastcall)) *)(void*, void*, int32_t);
using CharmAction = void (__attribute__((fastcall)) *)(void*, void*, int32_t, int32_t);
using EquipAction = void (__attribute__((fastcall)) *)(void*, void*, Address, uint8_t);
using AmmoAction = Address (__attribute__((fastcall)) *)(void*, void*, void*);
using ConsumableAction = int32_t (__attribute__((fastcall)) *)(void*, void*, uint8_t);
}

bridge::Value<bool> StateAdapter::NativeInput(uint32_t mask) noexcept {
    // Read-only equivalent of 00aa0710 for the verified DishonoredEngine.
    // Its +188 override is 004d8910 (xor eax,eax;ret). An alternate engine
    // or another mod replacing this gate is not assumed to have that behavior.
    Address engine{}, type{}, table{}, gate{};
    uint8_t disabledA{}, disabledB{}; uint32_t flags{};
    std::array<uint32_t, 11> layers{};
    if (!controller_ || !memory_.Read(memory_.Image() + EngineGlobal, engine) ||
        !memory_.Object(engine, type) || !memory_.Read(engine, table) ||
        table != memory_.Image() + EngineVtable || !memory_.Read(table + 0x188, gate) ||
        gate != memory_.Image() + EngineInputGate ||
        !memory_.Read(controller_ + 0x39e, disabledA) || !memory_.Read(controller_ + 0x39d, disabledB) ||
        !memory_.Read(controller_ + 0x610, flags) ||
        !memory_.Bytes(controller_ + 0x66c, layers.data(), sizeof(layers))) return {};
    if (disabledA) mask &= 0xfffffffd;
    if (disabledB) mask &= 0xffffecfe;
    if (flags & 0x1000) mask &= 0xfff81303;
    return {std::all_of(layers.begin(), layers.end(), [mask](uint32_t layer) { return (layer & mask) != 0; }), true};
}

bool StateAdapter::PendingApplied(const bridge::Snapshot& snapshot) const noexcept {
    const auto& command = pending_.command;
    switch (command.kind) {
        case bridge::CommandKind::SelectPower: {
            const auto* entry = Find(snapshot.powers, command.key);
            return entry && entry->selected.known && entry->selected.value;
        }
        case bridge::CommandKind::SelectEquipment: {
            const auto* entry = Find(snapshot.equipment, command.key);
            return entry && entry->selected.known && entry->selected.value;
        }
        case bridge::CommandKind::EquipCharm:
        case bridge::CommandKind::RemoveCharm: {
            const auto* entry = Find(snapshot.charms, command.key);
            return entry && entry->equipped.known &&
                entry->equipped.value == (command.kind == bridge::CommandKind::EquipCharm);
        }
        case bridge::CommandKind::ReadDocument: {
            const auto* entry = Find(snapshot.journal, command.key);
            return entry && entry->contentsComplete;
        }
        case bridge::CommandKind::PlayAudiograph:
        case bridge::CommandKind::StopAudiograph: {
            const auto* entry = Find(snapshot.journal, command.key);
            return entry && entry->playing.known && entry->playing.value == (command.kind == bridge::CommandKind::PlayAudiograph);
        }
        case bridge::CommandKind::SelectMap:
            return command.key ? snapshot.map.key == command.key && !snapshot.map.followingPlayer :
                snapshot.map.followingPlayer && snapshot.map.key != 0;
        default: return false;
    }
}

bridge::CommandResult StateAdapter::Apply(const bridge::Command& command,
    const bridge::Snapshot& snapshot, uint64_t now) noexcept {
    using Result = bridge::CommandResult;
    // Choosing an already acquired sheet is a panel view request. It does
    // not invoke a game action and remains usable with native actions off.
    if (!nativeActions_ && command.kind != bridge::CommandKind::SelectMap) return Result::Unsupported;
    memory_.Begin(32768, 2048);
    Address pawn{};
    if (!Roots(controller_, pawn) || pawn != pawn_) return Result::FailedValidation;
    if (command.kind == bridge::CommandKind::SelectMap) {
        const Result result = SelectMap(command.key, snapshot);
        if (result == Result::Applied) pending_ = {command, now, true};
        return result;
    }
    if (command.kind == bridge::CommandKind::ReadDocument) {
        const Result result = ReadDocument(command.key, snapshot);
        if (result == Result::Applied) pending_ = {command, now, true};
        return result;
    }
    if (command.kind == bridge::CommandKind::PlayAudiograph || command.kind == bridge::CommandKind::StopAudiograph) {
        const Result result = AudioLogAction(command, snapshot);
        if (result == Result::Applied) pending_ = {command, now, true};
        return result;
    }
    const auto eligibility = NativeInput(command.kind == bridge::CommandKind::SelectPower ? 0x800 :
        command.kind == bridge::CommandKind::UseConsumable ? 0x40 : 0x10);
    if (!eligibility.known) return Result::FailedValidation;
    if (!eligibility.value) return Result::RejectedEligibility;
    if (command.kind == bridge::CommandKind::UseConsumable) {
        const auto* entry = Find(snapshot.equipment, command.key);
        const uint32_t type = uint32_t(command.key) - 1;
        if (!entry || (command.key >> 32) != 0x301 || type > 1) return Result::RejectedOwnership;
        if (!entry->selectable.known || !entry->selectable.value) return Result::RejectedEligibility;
        Address inventory{}, owner{}; int32_t count{};
        if (!memory_.Read(pawn + 0x59c, inventory) || !memory_.IsA(inventory, u"DishonoredInventory") ||
            !memory_.Read(inventory + 0xdc, owner) || owner != pawn ||
            !memory_.Read(inventory + 0xd4 + type * 4, count)) return Result::FailedValidation;
        if (count <= 0) return Result::RejectedOwnership;
        // 00aaea90 performs the native heal/mana restoration, decrements the
        // inventory, emits gameplay events and plays the original sound.
        const int32_t applied = reinterpret_cast<ConsumableAction>(uintptr_t(memory_.Image() + ConsumeElixir))(
            reinterpret_cast<void*>(uintptr_t(controller_)), nullptr, uint8_t(type));
        return applied ? Result::Applied : Result::RejectedEligibility;
    }
    if (command.kind == bridge::CommandKind::SelectPower) {
        const auto* entry = Find(snapshot.powers, command.key);
        if (!entry || entry->passive || !entry->unlocked.known || !entry->unlocked.value)
            return Result::RejectedOwnership;
        if (entry->selected.known && entry->selected.value) return Result::Applied;
        if (!entry->selectable.known || !entry->selectable.value) return Result::RejectedEligibility;
        GameArray active{}, usable{}; uint8_t flags{};
        if (!memory_.Array(pawn + 0x788, active, 64, 4) ||
            !memory_.Array(pawn + PawnActivePowers, usable, 64, 4) ||
            !memory_.Read(pawn + 0x8a0, flags)) return Result::FailedValidation;
        if (flags & 0x10) return Result::RejectedEligibility;
        for (int32_t slot = 0; slot < usable.count; ++slot) {
            int32_t index{}; Address component{};
            if (!memory_.Read(usable.data + uint32_t(slot) * 4, index) || index < 0 || index >= active.count ||
                !memory_.Read(active.data + uint32_t(index) * 4, component) ||
                !memory_.IsA(component, u"DishonoredActivePowerComponent")) return Result::FailedValidation;
            if (Key(component, bridge::Page::Powers) != command.key) continue;
            // Native select toggles a selected power off. Re-read selection
            // immediately before calling so our explicit Select is idempotent.
            int32_t selected{};
            if (!SelectedPower(pawn, selected)) return Result::FailedValidation;
            if (selected == index) return Result::Applied;
            reinterpret_cast<UnaryAction>(uintptr_t(memory_.Image() + SelectPower))(
                reinterpret_cast<void*>(uintptr_t(controller_)), nullptr, slot);
            pending_ = {command, now, true};
            return Result::Applied; // Acknowledgement waits for the next real snapshot.
        }
        return Result::RejectedOwnership;
    }
    if (command.kind == bridge::CommandKind::SelectEquipment) {
        const auto* entry = Find(snapshot.equipment, command.key);
        if (entry && (command.key >> 32) == 0x300) {
            if (!entry->selectable.known || !entry->selectable.value) return Result::RejectedEligibility;
            return SelectAmmunition(command, now);
        }
        if (!entry || (command.key >> 32) != uint64_t(bridge::Page::Equipment) + 1)
            return Result::RejectedOwnership;
        if (entry->selected.known && entry->selected.value) return Result::Applied;
        if (!entry->selectable.known || !entry->selectable.value) return Result::RejectedEligibility;
        Address inventory{}; GameArray slots{};
        if (!memory_.Read(pawn + 0x59c, inventory) || !memory_.IsA(inventory, u"DishonoredInventory") ||
            !memory_.Array(inventory + 0x38, slots, 256, 12)) return Result::FailedValidation;
        for (int32_t i = 0; i < slots.count; ++i) {
            struct Slot { Address item, requiredClass; uint8_t usage{}, padding[3]{}; } slot{};
            if (!memory_.Read(slots.data + uint32_t(i) * 12, slot)) return Result::FailedValidation;
            if (!slot.item) continue;
            if (!memory_.IsA(slot.item, u"DishonoredInventoryItem")) return Result::FailedValidation;
            if (Key(slot.item, bridge::Page::Equipment) != command.key) continue;
            Address type{}, tweaks{}, player{}, selected{}, field{}, owner{};
            if ((slot.usage != 1 && slot.usage != 2) || !WeaponDefinition(slot.item, tweaks, player) ||
                !FieldAt(slot.item, u"m_pOwningInventory", u"ObjectProperty", 4, field) ||
                !memory_.Read(field, owner) || owner != inventory || !memory_.Object(slot.item, type) ||
                !SelectedItem(inventory, slot.usage, selected)) return Result::FailedValidation;
            if (selected == slot.item) return Result::Applied;
            // Native Dis_Equip validates the requested class/usage and schedules
            // the actual draw transition. Do not write selected-slot indices.
            reinterpret_cast<EquipAction>(uintptr_t(memory_.Image() + EquipItem))(
                reinterpret_cast<void*>(uintptr_t(controller_)), nullptr, type, slot.usage);
            pending_ = {command, now, true}; return Result::Applied;
        }
        return Result::RejectedOwnership;
    }
    if (command.kind == bridge::CommandKind::EquipCharm || command.kind == bridge::CommandKind::RemoveCharm) {
        const auto* entry = Find(snapshot.charms, command.key);
        if (!entry || (command.key >> 32) != 0x205 || !uint32_t(command.key)) return Result::RejectedOwnership;
        const bool equip = command.kind == bridge::CommandKind::EquipCharm;
        if (entry->equipped.known && entry->equipped.value == equip) return Result::Applied;
        const auto allowed = equip ? entry->canEquip : entry->canRemove;
        if (!allowed.known || !allowed.value) return Result::RejectedEligibility;
        Address tweaks{}; int32_t count{}; GameArray owned{};
        std::array<int32_t, 10> slots{};
        const uint32_t index = uint32_t(command.key) - 1;
        if (!PlayerTweaks(pawn, tweaks) || !CharmSlots(pawn, tweaks, count) ||
            !memory_.Array(pawn + PawnCollectedCharms, owned, 4096, 8) || index >= uint32_t(owned.count) ||
            !memory_.Bytes(pawn + 0xb10, slots.data(), uint32_t(count) * 4)) return Result::FailedValidation;
        for (int32_t i = 0; i < count; ++i)
            if (slots[i] < -1 || slots[i] >= owned.count ||
                (slots[i] != -1 && std::find(slots.begin(), slots.begin() + i, slots[i]) != slots.begin() + i))
                return Result::FailedValidation;
        const bool equipped = std::find(slots.begin(), slots.begin() + count, int32_t(index)) != slots.begin() + count;
        if (equipped == equip) return Result::Applied;
        if (equip) {
            const auto free = std::find(slots.begin(), slots.begin() + count, -1);
            if (free == slots.begin() + count) return Result::RejectedEligibility;
            // A validated free slot avoids the native replacement branch. The
            // function updates both charm effects and actual equipment slots.
            reinterpret_cast<CharmAction>(uintptr_t(memory_.Image() + EquipCharm))(
                reinterpret_cast<void*>(uintptr_t(pawn)), nullptr, int32_t(index), int32_t(free - slots.begin()));
        } else {
            reinterpret_cast<UnaryAction>(uintptr_t(memory_.Image() + RemoveCharm))(
                reinterpret_cast<void*>(uintptr_t(pawn)), nullptr, int32_t(index));
        }
        pending_ = {command, now, true};
        return Result::Applied;
    }
    return Result::Unsupported;
}

bridge::CommandResult StateAdapter::SelectAmmunition(const bridge::Command& command, uint64_t now) noexcept {
    using Result = bridge::CommandResult;
    const uint32_t type = uint32_t(command.key) - 1;
    if ((command.key >> 32) != 0x300 || type >= 12) return Result::RejectedOwnership;
    memory_.Begin(65536, 4096); Address pawn{};
    if (!Roots(controller_, pawn) || pawn != pawn_) return Result::FailedValidation;
    std::array<AmmoBinding, 12> bindings{};
    if (!AmmunitionBindings(pawn, bindings)) return Result::FailedValidation;
    const auto& binding = bindings[type];
    if (!binding.item || !binding.key || !binding.known) return Result::RejectedOwnership;
    Address inventory{}; GameArray amounts{}; int32_t count{};
    if (!memory_.Read(pawn + 0x59c, inventory) || !memory_.Array(inventory + 0xbc, amounts, 12, 8) ||
        type >= uint32_t(amounts.count) || !memory_.Read(amounts.data + type * 8, count)) return Result::FailedValidation;
    if (count <= 0) return Result::RejectedOwnership;
    const auto eligibility = NativeInput(binding.selected ? (binding.usage == 1 ? 4 : 8) : 0x10);
    if (!eligibility.known) return Result::FailedValidation;
    if (!eligibility.value) return Result::RejectedEligibility;
    if (binding.selected && binding.current == type) return Result::Applied;
    if (!binding.selected) {
        if (pending_.active && pending_.awaitingAmmoWeapon) return Result::RejectedBusy;
        Address itemClass{};
        if (!memory_.Object(binding.item, itemClass)) return Result::FailedValidation;
        reinterpret_cast<EquipAction>(uintptr_t(memory_.Image() + EquipItem))(
            reinterpret_cast<void*>(uintptr_t(controller_)), nullptr, itemClass, binding.usage);
        pending_ = {command, now, true, true}; return Result::Applied;
    }
    // Exact stack parameter object constructed by native Next/PreviousOption
    // (00c30929..3c). Passing the requested unlocked type through the same
    // context dispatcher preserves reload/animation and weapon state changes.
    struct Parameters { Address vtable; uint32_t ammoType, flags; } parameters = {
        memory_.Image() + AmmoOptionParameters, type, 0};
    reinterpret_cast<AmmoAction>(uintptr_t(memory_.Image() + AmmoOptionRequest))(
        reinterpret_cast<void*>(uintptr_t(binding.item)), nullptr, &parameters);
    const uint64_t started = pending_.active ? pending_.started : now;
    pending_ = {command, started, true, false}; return Result::Applied;
}

void StateAdapter::Commands(const bridge::Snapshot& snapshot, uint64_t now) noexcept {
    using Result = bridge::CommandResult;
    // Never invoke a native action twice while its animation/equipment request
    // is pending. Reserve acknowledgement space before taking any command.
    if (!bridge_.acknowledgements.HasSpace()) return;
    if (pending_.active) {
        const bool stale = pending_.command.generation != generation_ || !bridge::Fresh(snapshot, now);
        const bool applied = !stale && PendingApplied(snapshot);
        const bool timeout = now < pending_.started || now - pending_.started >= 1500;
        if (!stale && !applied && !timeout && pending_.awaitingAmmoWeapon) {
            const auto result = SelectAmmunition(pending_.command, now);
            if (result == Result::Applied || result == Result::RejectedBusy) return;
            bridge_.acknowledgements.Push({pending_.command.request, pending_.command.generation, result});
            bridge_.Record({Bridge::EventKind::ActionResult, now, pending_.command.generation, pending_.command.request,
                pending_.command.key, int32_t(pending_.command.kind), int32_t(result)});
            pending_.active = false; return;
        }
        if (!stale && !applied && !timeout) return;
        bridge_.acknowledgements.Push({pending_.command.request, pending_.command.generation,
            stale ? Result::RejectedStale : applied ? Result::Applied : Result::RejectedUnavailable});
        bridge_.Record({Bridge::EventKind::ActionResult, now, pending_.command.generation, pending_.command.request,
            pending_.command.key, int32_t(pending_.command.kind), int32_t(stale ? Result::RejectedStale : applied ? Result::Applied : Result::RejectedUnavailable)});
        pending_.active = false;
        // This tick's snapshot precedes any new mutation. Defer a new action
        // until the next tick, even after an acknowledgement has been sent.
        return;
    }
    bridge::Command command{};
    if (!bridge_.commands.Pop(command)) return;
    const bridge::Domain* domain = nullptr;
    switch (command.kind) {
        case bridge::CommandKind::SelectPower: domain = &snapshot.powers.domain; break;
        case bridge::CommandKind::SelectEquipment:
        case bridge::CommandKind::UseConsumable: domain = &snapshot.equipment.domain; break;
        case bridge::CommandKind::EquipCharm:
        case bridge::CommandKind::RemoveCharm: domain = &snapshot.charms.domain; break;
        case bridge::CommandKind::ReadDocument:
        case bridge::CommandKind::PlayAudiograph:
        case bridge::CommandKind::StopAudiograph: domain = &snapshot.journal.domain; break;
        case bridge::CommandKind::SelectMap: domain = &snapshot.map.domain; break;
    }
    Result result = Result::RejectedStale;
    if (domain && command.version == bridge::Version && command.generation == generation_ &&
        bridge::Fresh(snapshot, now) && command.issuedAt <= now && now - command.issuedAt <= bridge::FreshnessMilliseconds &&
        command.domainRevision == domain->revision) result = Apply(command, snapshot, now);
    if (!pending_.active) bridge_.acknowledgements.Push({command.request, command.generation, result});
    bridge_.Record({pending_.active ? Bridge::EventKind::ActionSubmitted : Bridge::EventKind::ActionResult,
        now, command.generation, command.request, command.key, int32_t(command.kind), int32_t(result)});
}
} // namespace dishonored

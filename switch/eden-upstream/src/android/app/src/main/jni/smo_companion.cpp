#include "jni/smo_companion.h"
#include "jni/smo_state_reader.h"
#include "jni/smo_moon_reader.h"
#include "jni/smo_marker_reader.h"
#include "jni/smo_collection_reader.h"
#include "jni/native.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/memory.h"
#include "core/loader/smo_outfit_protocol.h"
#include <chrono>

namespace SmoCompanion {
namespace {
Identity previous{};
Address previous_base{};
std::uint64_t generation{};
MoonReader moon_reader;
std::uint32_t outfit_ticket{};
std::uint32_t outfit_logged_state{SmoOutfit::Idle};
std::chrono::steady_clock::time_point outfit_queued_at;
SmoOutfit::Mailbox* OutfitMailbox(EmulationSession& session) {
    auto& system = session.System();
    if (!session.IsRunning() || system.GetApplicationProcessProgramID() != TitleId ||
        system.GetApplicationProcessBuildID() != BuildId) return nullptr;
    const auto address = system.GetApplicationProcessCompanionMailbox();
    auto& memory = system.ApplicationMemory();
    if (!address || (address & 7) || !memory.IsValidVirtualAddressRange(address, sizeof(SmoOutfit::Mailbox)))
        return nullptr;
    auto* bytes = memory.GetPointerSilent(address);
    if (!bytes || memory.GetPointerSilent(address + sizeof(SmoOutfit::Mailbox) - 1) !=
                      bytes + sizeof(SmoOutfit::Mailbox) - 1) return nullptr;
    auto* box = reinterpret_cast<SmoOutfit::Mailbox*>(bytes);
    if (box->magic != SmoOutfit::Magic || box->version != SmoOutfit::Version ||
        box->size != sizeof(*box)) return nullptr;
    return box;
}
struct MemoryAdapter {
    Core::Memory::Memory& memory;
    bool Read(Address address, void* out, std::size_t count) {
        return memory.IsValidVirtualAddressRange(address, count) &&
               memory.ReadBlockUnsafe(address, out, count);
    }
};
}
int QueueOutfit(EmulationSession& session, int kind, std::string_view name,
               std::uint64_t expected_generation) {
    if (kind < 0 || kind > 1 || name.empty() || name.size() >= 64) return -1;
    auto access = session.LockSystemAccess();
    auto* mailbox = OutfitMailbox(session);
    if (!mailbox || generation != expected_generation) return -1;
    auto& system = session.System();
    const auto [base, size] = system.GetApplicationProcessMainModule();
    if (!base || base != previous_base || size < ApplicationInstance + 8) return -1;
    MemoryAdapter memory{system.ApplicationMemory()};
    Snapshot snapshot; Identity identity;
    if (!Reader(memory, base).Read(snapshot, identity) || identity != previous || snapshot.health <= 0)
        return -1;
    SmoOutfit::Mailbox request{};
    std::memcpy(request.name, name.data(), name.size());
    if (!SmoOutfit::Identifier(request.name)) return -1;
    auto old = __atomic_load_n(&mailbox->state, __ATOMIC_ACQUIRE);
    if (SmoOutfit::Busy(old) ||
        !__atomic_compare_exchange_n(&mailbox->state, &old, SmoOutfit::Writing, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return -3;
    outfit_ticket = outfit_ticket >= 0x7ffffffe ? 1 : outfit_ticket + 1;
    mailbox->ticket = outfit_ticket; mailbox->kind = kind; mailbox->result = SmoOutfit::InvalidRequest;
    mailbox->scene = identity.scene; mailbox->holder = identity.holder;
    mailbox->save = identity.save; mailbox->player = identity.player;
    std::memcpy(mailbox->name, request.name, 64);
    std::memcpy(mailbox->expected_outfit, snapshot.outfit.data(), 64);
    std::memcpy(mailbox->expected_hat, snapshot.hat.data(), 64);
    std::memset(mailbox->stage, 0, sizeof(*mailbox) - offsetof(SmoOutfit::Mailbox, stage));
    outfit_queued_at = std::chrono::steady_clock::now();
    outfit_logged_state = SmoOutfit::Pending;
    __atomic_store_n(&mailbox->state, SmoOutfit::Pending, __ATOMIC_RELEASE);
    LOG_INFO(Frontend, "[SMO] Wardrobe request={} kind={} name={}", outfit_ticket, kind, name);
    return static_cast<int>(outfit_ticket);
}

int PollOutfit(EmulationSession& session, int ticket) {
    auto access = session.LockSystemAccess();
    auto* mailbox = OutfitMailbox(session);
    if (!mailbox || ticket <= 0 || mailbox->ticket != static_cast<std::uint32_t>(ticket)) return -1;
    auto current = __atomic_load_n(&mailbox->state, __ATOMIC_ACQUIRE);
    if (current != outfit_logged_state) {
        LOG_INFO(Frontend, "[SMO] Wardrobe request={} phase={}", ticket, current);
        outfit_logged_state = current;
    }
    if (current == SmoOutfit::Pending &&
        std::chrono::steady_clock::now() - outfit_queued_at > std::chrono::milliseconds(1500)) {
        // Cancel only unclaimed requests, never interrupt the native transition.
        if (__atomic_compare_exchange_n(&mailbox->state, &current, SmoOutfit::Cancelled, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            current = SmoOutfit::Cancelled;
    }
    if (current == SmoOutfit::Completed) {
        const auto* q = mailbox->rotation;
        LOG_INFO(Frontend, "[SMO] Wardrobe pose rotation=({},{},{},{}) norm2={}",
                 q[0], q[1], q[2], q[3], q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
        LOG_INFO(Frontend, "[SMO] Wardrobe result={} native={} stage={} spawns={} expected=({},{},{}) observed=({},{},{})",
                 ticket, mailbox->result, mailbox->stage, mailbox->spawn_count,
                 mailbox->position[0], mailbox->position[1], mailbox->position[2],
                 mailbox->observed_position[0], mailbox->observed_position[1], mailbox->observed_position[2]);
        return mailbox->result == SmoOutfit::Success || mailbox->result == SmoOutfit::AlreadyEquipped
                   ? 1 : -10 - static_cast<int>(mailbox->result);
    }
    if (current == SmoOutfit::Cancelled) return -2;
    return 0;
}

int FillSnapshot(EmulationSession& session, void* output, std::size_t capacity) {
    if (!output || capacity < sizeof(Snapshot)) return -1;
    auto access = session.LockSystemAccess();
    static Status logged_status{Status::NotRunning};
    Snapshot frame;
    auto& system = session.System();
    const auto [base, size] = session.IsRunning() ? system.GetApplicationProcessMainModule()
                                                 : std::pair<u64, u64>{};
    if (!session.IsRunning()) frame.status = Status::NotRunning;
    else if (system.GetApplicationProcessProgramID() != TitleId) frame.status = Status::WrongTitle;
    else if (system.GetApplicationProcessBuildID() != BuildId) frame.status = Status::UnsupportedBuild;
    else if (base && size >= ApplicationInstance + 8) {
        MemoryAdapter memory{system.ApplicationMemory()};
        Identity identity{};
        Reader reader{memory, base};
        if (reader.Read(frame, identity)) {
            if (identity != previous || previous_base != base) {
                ++generation;
                previous = identity;
                previous_base = base;
                LOG_INFO(Frontend, "[SMO] Gameplay identity generation={} world={} scenario={}",
                         generation, frame.world, frame.scenario);
            }
        } else frame.status = Status::Transition;
    }
    if (frame.status != Status::Active) {
        previous = {};
        previous_base = 0;
        if (frame.status == Status::NotRunning || frame.status == Status::WrongTitle ||
            frame.status == Status::UnsupportedBuild) moon_reader.Reset();
    }
    frame.generation = generation;
    if (logged_status != frame.status) {
        LOG_INFO(Frontend, "[SMO] Reader status={}", static_cast<u32>(frame.status));
        logged_status = frame.status;
    }
    std::memcpy(output, &frame, sizeof(frame));
    return sizeof(frame);
}

int FillMoons(EmulationSession& session, void* output, std::size_t capacity) {
    if (!output || capacity < sizeof(MoonSnapshot)) return -1;
    auto access = session.LockSystemAccess();
    MoonSnapshot packet;
    auto& system = session.System();
    if (session.IsRunning() && system.GetApplicationProcessProgramID() == TitleId &&
        system.GetApplicationProcessBuildID() == BuildId) {
        const auto [base, size] = system.GetApplicationProcessMainModule();
        if (base && base == previous_base && size >= ApplicationInstance + 8) {
            MemoryAdapter memory{system.ApplicationMemory()};
            Snapshot state; Identity identity;
            if (Reader(memory, base).Read(state, identity) && identity == previous) {
                state.generation = generation;
                moon_reader.Read(memory, base, identity, state, packet);
            }
        }
    }
    std::memcpy(output, &packet, sizeof(packet));
    return sizeof(packet);
}
int FillMarkers(EmulationSession& session, void* output, std::size_t capacity) {
    if (!output || capacity < sizeof(MarkerSnapshot)) return -1;
    auto access = session.LockSystemAccess();
    MarkerSnapshot packet;
    auto& system = session.System();
    if (session.IsRunning() && system.GetApplicationProcessProgramID() == TitleId &&
        system.GetApplicationProcessBuildID() == BuildId) {
        const auto [base, size] = system.GetApplicationProcessMainModule();
        if (base && base == previous_base && size >= ApplicationInstance + 8) {
            MemoryAdapter memory{system.ApplicationMemory()};
            Snapshot state; Identity identity;
            if (Reader(memory, base).Read(state, identity) && identity == previous) {
                state.generation = generation;
                MarkerReader(memory).Read(base, identity, state, packet);
            }
        }
    }
    std::memcpy(output, &packet, sizeof(packet));
    return sizeof(packet);
}
int FillCollection(EmulationSession& session, void* output, std::size_t capacity, int kind) {
    if (!output || capacity < sizeof(CollectionSnapshot) || kind < 0 || kind > 5) return -1;
    auto access = session.LockSystemAccess();
    CollectionSnapshot packet;
    auto& system = session.System();
    if (session.IsRunning() && system.GetApplicationProcessProgramID() == TitleId &&
        system.GetApplicationProcessBuildID() == BuildId) {
        const auto [base, size] = system.GetApplicationProcessMainModule();
        if (base && base == previous_base && size >= ApplicationInstance + 8) {
            MemoryAdapter memory{system.ApplicationMemory()};
            Snapshot state; Identity identity;
            if (Reader(memory, base).Read(state, identity) && identity == previous) {
                state.generation = generation;
                CollectionReader(memory).Read(base, identity, state, static_cast<CollectionKind>(kind), packet);
            }
        }
    }
    std::memcpy(output, &packet, sizeof(packet));
    return sizeof(packet);
}
} // namespace SmoCompanion

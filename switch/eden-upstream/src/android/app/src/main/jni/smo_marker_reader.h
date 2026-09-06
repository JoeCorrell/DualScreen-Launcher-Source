#pragma once
#include "smo_state_reader.h"

namespace SmoCompanion {
enum class MarkerKind : std::uint32_t { Checkpoint, Odyssey, Shop, Minigame };
struct MapMarker {
    std::uint32_t id{};
    MarkerKind kind{};
    std::array<float, 3> position{};
    std::uint32_t reserved{};
    std::array<char, 64> stage{}, label{};
};
struct MarkerSnapshot {
    std::uint32_t magic{0x4b4f4d53}, version{1}, size{4896}, count{};
    std::uint64_t generation{};
    std::int32_t world{-1}, scenario{-1};
    std::array<MapMarker, 32> entries{};
};
static_assert(sizeof(MapMarker) == 152 && sizeof(MarkerSnapshot) == 4896);

// MapLayout::appearParts (0x1f342c), observed through game-owned save data.
// Does not depend on MapLayout existing. No calls or writes into the guest.
template<class Memory> class MarkerReader {
public:
    explicit MarkerReader(Memory& memory_) : memory(memory_) {}
    bool Read(Address base, const Identity& owner, const Snapshot& state, MarkerSnapshot& out) {
        bytes = 0;
        MarkerSnapshot next;
        if (state.status != Status::Active || !state.map_valid || state.world < 0 || state.world >= 17)
            return false;
        const auto race = Value<std::int32_t>(owner.save + 0xa44);
        const auto balloon = Pointer(owner.save + 0xa30);
        const auto balloon_state = balloon ? Value<std::int32_t>(*balloon + 0x1d0) : std::nullopt;
        if (!race || !balloon_state) return false;
        // Native map suppresses ordinary icons while racing / Balloon World.
        if (!*race && *balloon_state != 2) {
            if (!Checkpoints(owner, state, next) || !Shops(owner, state, next) ||
                !Minigames(owner, state, next) || !Home(owner, state, next)) return false;
        }
        Snapshot after; Identity identity;
        if (!Reader(memory, base).Read(after, identity) || identity != owner ||
            after.world != state.world || after.scenario != state.scenario ||
            after.map_scenario != state.map_scenario || !after.map_valid ||
            Value<std::int32_t>(owner.save + 0xa44) != race ||
            Pointer(owner.save + 0xa30) != balloon ||
            Value<std::int32_t>(*balloon + 0x1d0) != balloon_state) return false;
        next.generation = state.generation;
        next.world = state.world; next.scenario = state.scenario;
        out = next;
        return true;
    }
private:
    bool Checkpoints(const Identity& owner, const Snapshot& state, MarkerSnapshot& out) {
        // isGotCheckpointInWorld 0x521300: scenario-filtered ordinal, 17 x 0x148.
        const auto override = Value<std::int32_t>(owner.save + 0xa28);
        const auto worlds = Pointer(owner.save + 0x9e8);
        const auto records = worlds ? Pointer(*worlds + state.world * 8) : std::nullopt;
        std::array<char, 64> save_stage{};
        // Reader currently exposes maps only in their main stage. Require the save
        // stage to match, so native main-stage scenario semantics are exact here.
        if (!override || !records || !String(owner.save + 0x330, save_stage) ||
            std::strcmp(save_stage.data(), state.stage.data()) != 0) return false;
        const auto scenario = *override == -1 ? state.scenario : *override;
        if (scenario < 0 || scenario > 31) return false;
        std::uint32_t ordinal{};
        for (unsigned i = 0; i < 17; ++i) {
            const auto record = *records + i * 0x148;
            const auto mask = Value<std::uint32_t>(record + 0x130);
            const auto got = Value<std::uint8_t>(record + 0x140);
            if (!mask || !got) return false;
            if (!(*mask & (std::uint32_t{1} << scenario))) continue;
            const auto id = ordinal++;
            if (!*got) continue; // Never expose undiscovered names or coordinates.
            MapMarker marker; marker.id = id; marker.kind = MarkerKind::Checkpoint;
            if (!String(record + 8, marker.stage) || !String(record + 0xa0, marker.label) ||
                !Position(record + 0x134, marker.position)) return false;
            // Interior checkpoints require native entrance substitution, not XYZ
            // projected onto the main-stage map.
            if (std::strcmp(marker.stage.data(), state.stage.data()) == 0) Add(out, marker);
        }
        return Pointer(owner.save + 0x9e8) == worlds &&
            Pointer(*worlds + state.world * 8) == records &&
            Value<std::int32_t>(owner.save + 0xa28) == override;
    }
    bool Shops(const Identity& owner, const Snapshot& state, MarkerSnapshot& out) {
        // calcShopNum 0x524bc8: contiguous nonempty registered shop names, max 4.
        const auto records = Pointer(owner.save + 0xa58);
        if (!records) return false;
        for (unsigned i = 0; i < 4; ++i) {
            MapMarker marker; marker.id = 0x100 + i; marker.kind = MarkerKind::Shop;
            marker.stage = state.stage;
            const auto record = *records + i * 0x68;
            if (!String(record + 8, marker.label)) return false;
            if (!marker.label[0]) break;
            // getShopNpcTrans 0x5248e8. Sell-out affects icon style, not visibility.
            if (!Position(record + 0x58, marker.position)) return false;
            Add(out, marker);
        }
        return Pointer(owner.save + 0xa58) == records;
    }
    bool Minigames(const Identity& owner, const Snapshot& state, MarkerSnapshot& out) {
        // calcMiniGameNum/getMiniGameTrans/getMiniGameName 0x52483c/868/878.
        const auto records = Pointer(owner.save + 0xa68);
        if (!records) return false;
        unsigned count{};
        for (unsigned i = 0; i < 4; ++i) {
            const auto present = Value<std::uint8_t>(*records + i * 0x68 + 0xc);
            if (!present || *present > 1) return false;
            count += *present;
        }
        for (unsigned i = 0; i < count; ++i) {
            MapMarker marker; marker.id = 0x200 + i; marker.kind = MarkerKind::Minigame;
            marker.stage = state.stage;
            if (!Position(*records + i * 0x68, marker.position) ||
                !String(*records + i * 0x68 + 0x18, marker.label)) return false;
            Add(out, marker);
        }
        return Pointer(owner.save + 0xa68) == records;
    }
    bool Home(const Identity& owner, const Snapshot& state, MarkerSnapshot& out) {
        // isExistHome 0x52c2c0, isGameClear 0x51c71c. Not a guessed story threshold.
        const auto progress = Pointer(owner.save + 0x6a8);
        const auto unlocked = progress ? Pointer(*progress + 0x20) : std::nullopt;
        const auto first_visit = progress ? Pointer(*progress + 0x48) : std::nullopt;
        const auto activated = progress ? Value<std::int32_t>(*progress + 0x34) : std::nullopt;
        if (!unlocked || !first_visit || !activated) return false;
        const auto peach = Value<std::uint8_t>(*unlocked + 14);
        const auto peach_first = Value<std::uint8_t>(*first_visit + 14);
        const auto current = Value<std::uint8_t>(*unlocked + state.world);
        if (!peach || !peach_first || !current) return false;
        if ((*peach && !*peach_first) || (*activated > 0 && *current)) {
            MapMarker marker; marker.id = 0x300; marker.kind = MarkerKind::Odyssey;
            marker.stage = state.stage;
            if (!Position(owner.save + 0xa7c, marker.position)) return false;
            Add(out, marker);
        }
        return Pointer(owner.save + 0x6a8) == progress;
    }
    void Add(MarkerSnapshot& out, const MapMarker& marker) {
        if (out.count < out.entries.size()) out.entries[out.count++] = marker;
    }
    bool Bytes(Address at, void* dest, std::size_t size) {
        if (!at || at >= (Address{1} << 39) || size > (Address{1} << 39) - at ||
            size > 512 || bytes + size > 8192) return false;
        bytes += size; return memory.Read(at, dest, size);
    }
    template<class T> std::optional<T> Value(Address at) {
        T value{}; if (!Bytes(at, &value, sizeof(value))) return std::nullopt; return value;
    }
    std::optional<Address> Pointer(Address at) {
        const auto value = Value<Address>(at);
        return value && *value && *value < (Address{1} << 39) ? value : std::nullopt;
    }
    bool String(Address at, std::array<char, 64>& out) {
        const auto ptr = Pointer(at);
        if (!ptr || !Bytes(*ptr, out.data(), out.size())) return false;
        const auto end = std::memchr(out.data(), 0, out.size());
        if (!end) return false;
        // Do not leak bytes after a native string's terminator into the packet.
        const auto length = static_cast<const char*>(end) - out.data();
        for (std::ptrdiff_t i = 0; i < length; ++i) if (out[i] < 32 || out[i] > 126) return false;
        std::memset(out.data() + length, 0, out.size() - length);
        return true;
    }
    bool Position(Address at, std::array<float, 3>& out) {
        if (!Bytes(at, out.data(), sizeof(out))) return false;
        for (float v : out) if (!std::isfinite(v)) return false;
        return true;
    }
    Memory& memory;
    std::size_t bytes{};
};
} // namespace SmoCompanion

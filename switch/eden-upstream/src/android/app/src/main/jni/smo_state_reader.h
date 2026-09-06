#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <optional>

// Exact-base, read-only contracts traced from the local NSO and checked against
// a menu-independent live capture. No Android/emulator dependencies, for tests.
namespace SmoCompanion {
using Address = std::uint64_t;
constexpr Address TitleId = 0x0100000000010000;
constexpr std::array<std::uint8_t, 32> BuildId{
    0x3c, 0xa1, 0x2d, 0xfa, 0xaf, 0x9c, 0x82, 0xda,
    0x06, 0x4d, 0x16, 0x98, 0xdf, 0x79, 0xcd, 0xa1};
constexpr Address ApplicationInstance = 0x1e665c8;
enum class Status : std::uint32_t { NotRunning, WrongTitle, UnsupportedBuild, Loading, Active, Transition };

struct Snapshot {
    std::uint32_t magic{0x534f4d53}; // SMOS
    std::uint32_t version{1};
    std::uint32_t size{416};
    Status status{Status::Loading};
    std::uint64_t generation{};
    std::int32_t world{-1}, scenario{-1}, coins{}, regional_coins{}, health{}, max_health{};
    std::array<float, 3> position{};
    std::int32_t world_moons{-1};
    std::array<char, 64> outfit{}, hat{}, stage{};
    std::uint32_t map_valid{};
    std::int32_t map_scenario{-1};
    std::uint32_t map_width{}, map_height{};
    std::array<char, 64> map_texture{};
    std::array<float, 16> map_view_projection{};
    std::array<float, 3> map_position{};
    std::int32_t total_moons{-1};
};
static_assert(sizeof(Snapshot) == 416);
static_assert(offsetof(Snapshot, outfit) == 64 && offsetof(Snapshot, stage) == 192);

struct Identity {
    Address application{}, game{}, sequence{}, scene{}, holder{}, save{}, player{};
    bool operator==(const Identity&) const = default;
};

// Memory adapter must return false on an unreadable range, not fabricate zeros.
template <class Memory> class Reader {
public:
    Reader(Memory& memory_, Address base_) : memory(memory_), base(base_) {}

    bool Read(Snapshot& output, Identity& identity) {
        read_bytes = 0;
        Snapshot candidate;
        Identity before{}, after{};
        if (!Roots(before)) return false;
        const auto coins = Value<std::int32_t>(before.save + 0x5d8);
        const auto world = Value<std::int32_t>(before.save + 0x9f0);
        const auto scenario = Value<std::int32_t>(before.save + 0xa24);
        const auto life = Pointer(before.save + 0x860);
        const auto pose = Pointer(before.player + 0x50);
        if (!coins || !world || !scenario || !life || !pose || *coins < 0 ||
            *world < 0 || *world >= 17 || *scenario < 0) return false;
        const auto health = Value<std::int32_t>(*life + 0xc);
        const auto assist = Value<std::uint8_t>(*life + 8);
        const auto extended = Value<std::uint8_t>(*life + 0x10);
        const auto force_normal = Value<std::uint8_t>(*life + 0x11);
        if (!health || !assist || !extended || !force_normal || *health < 0 || *health > 9 ||
            !Bytes(*pose + 8, candidate.position.data(), sizeof(candidate.position))) return false;
        for (float v : candidate.position) if (!std::isfinite(v)) return false;
        const auto total = Pointer(before.save + 0xa00);
        const auto spent = Pointer(before.save + 0x5e8);
        if (!total || !spent) return false;
        const auto total_value = Value<std::int32_t>(*total + *world * 4);
        const auto spent_value = Value<std::int32_t>(*spent + *world * 4);
        if (!total_value || !spent_value || *spent_value < 0 || *total_value < *spent_value)
            return false;
        if (!String(before.save + 0x718, candidate.outfit) ||
            !String(before.save + 0x770, candidate.hat) ||
            !String(before.scene + 0xe0, candidate.stage)) return false;
        // Same predicates as PlayerHitPointData::getMaxCurrent (0x538874).
        candidate.max_health = *force_normal ? 3 : (*assist ? 6 : 3) + (*extended ? 3 : 0);
        candidate.health = *health;
        candidate.coins = *coins;
        candidate.world = *world;
        candidate.scenario = *scenario;
        candidate.regional_coins = *total_value - *spent_value;
        // GameDataFile::getShineNum / getTotalShineNum (0x51f094 / 0x51f0c0).
        // Total uses all twenty native counters, not the number of visible list entries.
        const auto moon_counts = Pointer(before.save + 0x858);
        std::array<std::int32_t, 20> moons{};
        if (moon_counts && Bytes(*moon_counts, moons.data(), sizeof(moons))) {
            std::int32_t sum{};
            bool valid = true;
            for (auto count : moons) {
                if (count < 0 || count > 9999) { valid = false; break; }
                sum += count;
            }
            if (valid) { candidate.world_moons = moons[*world]; candidate.total_moons = sum; }
        }
        ReadMap(before, candidate); // Optional: an unavailable map never hides player status.
        // The emulator lock protects session teardown, not all guest updates.
        // Re-resolve owners, player and save identity; reject mixed scene frames.
        if (!Roots(after) || before != after ||
            Value<std::int32_t>(before.save + 0x9f0) != world ||
            Value<std::int32_t>(before.save + 0xa24) != scenario) return false;
        candidate.status = Status::Active;
        identity = before;
        output = candidate;
        return true;
    }

private:
    void ReadMap(const Identity& identity, Snapshot& frame) {
        const auto list = Pointer(identity.holder + 0x190);
        const auto worlds = list ? Value<std::int32_t>(*list) : std::nullopt;
        if (!worlds || *worlds <= frame.world || *worlds > 20) return;
        const auto entries = list ? Pointer(*list + 8) : std::nullopt;
        const auto entry = entries ? Pointer(*entries + frame.world * 8) : std::nullopt;
        std::array<char, 64> main_stage{};
        if (!entry || !String(*entry, main_stage)) return;
        // Substage entrance substitution and area overrides are not verified yet.
        // Do not put Mario's interior coordinates onto the overworld map.
        if (std::strcmp(main_stage.data(), frame.stage.data()) != 0) return;
        const auto maps = Pointer(identity.holder + 0x1e0);
        if (!maps || Pointer(*maps + 0x10) != std::optional<Address>{identity.holder}) return;
        const auto count = Value<std::int32_t>(*maps);
        const auto array = Pointer(*maps + 8);
        if (!count || *count <= frame.world || *count > 32 || !array) return;
        auto selected = Pointer(*array + frame.world * 8);
        const auto scenarios = Pointer(identity.save + 0x7d8);
        if (!selected || !scenarios) return;
        const auto scenario = Value<std::int32_t>(*scenarios + frame.world * 4);
        const auto child_count = Value<std::int32_t>(*selected + 0xd8);
        if (!scenario || !child_count || *child_count < 0 || *child_count > 32) return;
        if (*child_count) {
            const auto children = Pointer(*selected + 0xe0);
            if (!children) return;
            for (int i = 0; i < *child_count; ++i) {
                const auto child = Pointer(*children + i * 8);
                if (!child) return;
                if (Value<std::int32_t>(*child + 0xd0) == scenario) { selected = child; break; }
            }
        }
        const auto variant = Value<std::int32_t>(*selected + 0xd0);
        const auto width = Value<std::uint32_t>(*selected + 0xb8);
        const auto height = Value<std::uint32_t>(*selected + 0xbc);
        if (!variant || !width || !height || !*width || !*height || *width > 2048 || *height > 2048)
            return;
        // loadMapData's BYML key at 0x1850dc5 is ViewProjMatrix: offset ZERO.
        // +0x40 is ViewMatrix; +0x70 is ProjMatrix (not world projection).
        if (!Bytes(*selected, frame.map_view_projection.data(), 64)) return;
        for (float v : frame.map_view_projection) if (!std::isfinite(v)) return;
        if (frame.map_view_projection[15] != 1.f ||
            frame.map_view_projection[12] != 0.f || frame.map_view_projection[13] != 0.f ||
            frame.map_view_projection[14] != 0.f) return;
        float axes{};
        for (int i : {0, 1, 2, 4, 5, 6}) axes += std::abs(frame.map_view_projection[i]);
        if (axes == 0.f) return;
        // Native loadMapData formats <mainStage><scenario> only for positive variants.
        int length = *variant > 0 ? std::snprintf(frame.map_texture.data(), 64, "%s%d",
            main_stage.data(), *variant) : std::snprintf(frame.map_texture.data(), 64, "%s", main_stage.data());
        if (length < 1 || length >= 64) return;
        frame.map_scenario = *variant;
        frame.map_width = *width;
        frame.map_height = *height;
        frame.map_position = frame.position;
        frame.map_valid = 1;
    }

    bool Bytes(Address address, void* data, std::size_t size) {
        if (!address || address >= (Address{1} << 39) || size > (Address{1} << 39) - address ||
            size > 4096 || read_bytes + size > 8192) return false;
        read_bytes += size;
        return memory.Read(address, data, size);
    }
    template <class T> std::optional<T> Value(Address address) {
        T value{};
        if (!Bytes(address, &value, sizeof(value))) return std::nullopt;
        return value;
    }
    std::optional<Address> Pointer(Address address, std::size_t alignment = 8) {
        auto value = Value<Address>(address);
        if (!value || !*value || *value % alignment || *value >= (Address{1} << 39))
            return std::nullopt;
        return value;
    }
    Address Object(Address address, Address vtable_point) {
        auto object = Pointer(address);
        if (!object || Pointer(*object) != std::optional<Address>{base + vtable_point}) return 0;
        return *object;
    }
    bool String(Address address, std::array<char, 64>& destination) {
        auto source = Pointer(address, 1); // Native char buffers need only byte alignment.
        if (!source || !Bytes(*source, destination.data(), destination.size())) return false;
        return std::memchr(destination.data(), 0, destination.size()) != nullptr;
    }
    bool Roots(Identity& out) {
        out.application = Pointer(base + ApplicationInstance).value_or(0);
        if (!out.application) return false;
        const auto framework = Pointer(out.application + 0x28);
        const auto task_manager = framework ? Pointer(*framework + 0x38) : std::nullopt;
        const auto root = task_manager ? Pointer(*task_manager + 0x128) : std::nullopt;
        if (!root) return false;
        out.game = Object(*root + 0x200, 0x1d99828);
        if (!out.game) return false;
        out.sequence = Object(out.game + 0x10, 0x1d96ff0);
        if (!out.sequence) return false;
        out.scene = Object(out.sequence + 0xb0, 0x1d8bb80);
        if (!out.scene) return false;
        out.holder = Object(out.scene + 0x2d0, 0x1d996c8);
        const auto objects = Pointer(out.scene + 0xa0);
        const auto array = objects ? Pointer(*objects + 8) : std::nullopt;
        if (!out.holder || !array || Pointer(*array + 0x90) != std::optional<Address>{out.holder})
            return false;
        out.save = Pointer(out.holder + 0x20).value_or(0);
        const auto scene_info = Pointer(out.scene + 0x90);
        const auto players = scene_info ? Pointer(*scene_info + 0x98) : std::nullopt;
        if (!out.save || !players) return false;
        const auto capacity = Value<std::int32_t>(*players + 8);
        const auto count = Value<std::int32_t>(*players + 12);
        const auto records = Pointer(*players);
        if (!capacity || !count || !records || *count <= 0 || *capacity < *count || *capacity > 4)
            return false;
        // Constructor installs +0x18 because Mario's type has virtual bases.
        out.player = Object(*records, 0x1d779e8);
        return out.player != 0;
    }
    Memory& memory;
    Address base;
    std::size_t read_bytes{};
};
} // namespace SmoCompanion

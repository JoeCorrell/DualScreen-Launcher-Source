#pragma once
#include "smo_state_reader.h"
#include <string>
#include <vector>
#include <algorithm>

namespace SmoCompanion {
enum class CollectionKind : std::uint32_t { Outfit, Hat, Souvenir, Sticker, Capture, Music };
struct CollectionEntry {
    std::uint32_t id{}, owned{};
    std::array<char, 64> name{}, variant{};
    bool operator==(const CollectionEntry&) const = default;
};
struct CollectionSnapshot {
    std::uint32_t magic{0x434f4d53}, version{1}, size{34848}, count{};
    std::uint64_t generation{};
    std::int32_t world{-1};
    CollectionKind kind{};
    std::array<CollectionEntry, 256> entries{};
};
static_assert(sizeof(CollectionEntry) == 136 && sizeof(CollectionSnapshot) == 34848);

// Exact-base static catalog, extracted from SystemData/HackObjList.byml in the
// user's RomFS, preserving native order and excluding IsDebug just as
// HackListLayout::ctor (0x1ebd60) does. This is NOT ownership/discovery state.
inline constexpr const char* CaptureCatalog[] = {
    "Frog", "ElectricWire", "KuriboWing", "Wanwan", "WanwanBig", "BreedaWanwan",
    "TRex", "Fukankun", "Killer", "Megane", "Cactus", "Kuribo", "BossKnuckleHand",
    "BazookaElectric", "Kakku", "JugemFishing", "Fastener", "Pukupuku", "GotogotonLake",
    "PackunPoison", "Senobi", "FireBros", "Tank", "Gamane", "Tree", "RockForest",
    "FukuwaraiFacePartsKuribo", "Imomu", "Guidepost", "Manhole", "Car", "Radicon",
    "Byugo", "Yukimaru", "PukupukuSnow", "Hosui", "Bubble", "HackFork", "HammerBros",
    "CarryMeat", "PackunFire", "Tsukkun", "Statue", "StatueKoopa", "KaronWing",
    "KillerMagnum", "Bull", "Koopa", "AnagramAlphabetCharacter", "GotogotonCity",
    "FukuwaraiFacePartsMario", "Yoshi"
};

// One requested category, on the background reader only; no menu instances,
// memory scans, guest calls or save writes. Exact title/build gate is in caller.
template<class Memory> class CollectionReader {
public:
    explicit CollectionReader(Memory& m) : memory(m) {}
    bool Read(Address base, const Identity& owner, const Snapshot& state,
              CollectionKind kind, CollectionSnapshot& out) {
        if (state.status != Status::Active || static_cast<unsigned>(kind) > 5) return false;
        bytes = 0;
        CollectionSnapshot first, second;
        // Double sampling catches list mutation as well as owner changes. Strings
        // and flags are published atomically; hidden entry names never leave JNI.
        if (!Sample(base, owner, kind, first) || !Sample(base, owner, kind, second) ||
            first.count != second.count || first.entries != second.entries) return false;
        Snapshot after; Identity identity;
        if (!Reader(memory, base).Read(after, identity) || identity != owner ||
            after.world != state.world || after.scenario != state.scenario) return false;
        first.generation = state.generation; first.world = state.world; first.kind = kind;
        out = first; return true;
    }
private:
    using Text = std::array<char, 64>;
    bool Bytes(Address at, void* out, std::size_t size) {
        if (!at || at >= (Address{1} << 39) || size > (Address{1} << 39) - at ||
            size > 512 || bytes + size > 131072) return false;
        bytes += size; return memory.Read(at, out, size);
    }
    template<class T> std::optional<T> Value(Address at) {
        T value{}; if (!Bytes(at, &value, sizeof(value))) return {}; return value;
    }
    std::optional<Address> Pointer(Address at) {
        const auto p = Value<Address>(at);
        return p && *p && *p < (Address{1} << 39) ? p : std::nullopt;
    }
    bool StringAt(Address at, Text& out) {
        if (!Bytes(at, out.data(), out.size())) return false;
        auto end = std::find(out.begin(), out.end(), 0);
        if (end == out.end()) return false;
        for (auto i = out.begin(); i != end; ++i)
            if (!((*i >= 'A' && *i <= 'Z') || (*i >= 'a' && *i <= 'z') ||
                  (*i >= '0' && *i <= '9') || *i == '_')) return false;
        std::fill(end, out.end(), 0); return true;
    }
    bool String(Address at, Text& out, bool nullable = false) {
        const auto p = Value<Address>(at);
        if (nullable && p && !*p) { out = {}; return true; }
        return p && StringAt(*p, out);
    }
    bool Owned(Address save, Address field, unsigned limit, unsigned stride, std::vector<Text>& names) {
        const auto records = Pointer(save + field);
        if (!records) return false;
        for (unsigned i = 0; i < limit; ++i) {
            Text name;
            if (!String(*records + i * stride + 8, name)) return false;
            if (!name[0]) break;
            if (std::find(names.begin(), names.end(), name) != names.end()) return false;
            names.push_back(name);
        }
        return Pointer(save + field) == records;
    }
    bool Sample(Address base, const Identity& owner, CollectionKind kind, CollectionSnapshot& out) {
        if (kind == CollectionKind::Music) return Music(base, owner, out);
        std::vector<Text> owned;
        if (kind == CollectionKind::Capture) {
            // GameDataFile::isExistInHackDictionary (0x523cb4), 160 x 0x98.
            if (!Owned(owner.save, 0x7c8, 160, 0x98, owned)) return false;
            for (const auto name : CaptureCatalog) {
                CollectionEntry entry; entry.id = out.count;
                Text key{}; std::strncpy(key.data(), name, key.size()-1);
                entry.owned = std::find(owned.begin(), owned.end(), key) != owned.end();
                if (entry.owned) entry.name = key;
                out.entries[out.count++] = entry;
            }
            return true;
        }
        // rs::isHaveCloth/Cap/Gift/Sticker, GameDataFile::isBuyItem (0x5235d4).
        constexpr Address fields[]{0x6f8, 0x6f0, 0x708, 0x700};
        const unsigned k = static_cast<unsigned>(kind);
        if (!Owned(owner.save, fields[k], 64, 0x58, owned)) return false;
        const auto count = Value<std::int32_t>(owner.holder + 0x110 + k * 16);
        const auto catalog = Pointer(owner.holder + 0x118 + k * 16);
        if (!count || *count < 0 || *count > 64 || (*count && !catalog)) return false;
        for (int i = 0; i < *count; ++i) {
            const auto item = Pointer(*catalog + i * 8);
            CollectionEntry entry; entry.id = i; Text key{};
            if (!item || !StringAt(*item + 4, key) || !key[0]) return false;
            entry.owned = std::find(owned.begin(), owned.end(), key) != owned.end();
            if (entry.owned) entry.name = key;
            out.entries[out.count++] = entry;
        }
        return Value<std::int32_t>(owner.holder + 0x110 + k*16) == count &&
            Pointer(owner.holder + 0x118 + k*16) == catalog;
    }
    bool Music(Address base, const Identity& owner, CollectionSnapshot& out) {
        // Native updateList 0x4d84e4 iterates cCollectBgmListSize (GOT +1e4ecf8),
        // getCollectBgmByIndex 0x526494, 0x28 records (GOT +1e4ed00).
        // isCollectedBgm 0x526434 matches (name,situation) in file+830 PtrArray.
        const auto count_ptr = Pointer(base + 0x1e4ecf8);
        const auto catalog = Pointer(base + 0x1e4ed00);
        const auto count = count_ptr ? Value<std::int32_t>(*count_ptr) : std::nullopt;
        const auto saved_count = Value<std::int32_t>(owner.save + 0x830);
        const auto capacity = Value<std::int32_t>(owner.save + 0x834);
        const auto saved = Pointer(owner.save + 0x838);
        if (!count || *count < 0 || *count > 256 || !catalog || !saved_count ||
            !capacity || *saved_count < 0 || *saved_count > *capacity || *capacity > 256 ||
            (*saved_count && !saved)) return false;
        std::vector<CollectionEntry> records;
        for (int i = 0; i < *saved_count; ++i) {
            const auto p = Pointer(*saved + i*8);
            CollectionEntry entry;
            const auto flag = p ? Value<std::uint8_t>(*p + 0x10) : std::nullopt;
            if (!p || !flag || *flag > 1 || !String(*p, entry.name) ||
                !String(*p+8, entry.variant, true)) return false;
            entry.owned = *flag; records.push_back(entry);
        }
        for (int i = 0; i < *count; ++i) {
            CollectionEntry entry; entry.id = i;
            Text name{}, variant{};
            if (!String(*catalog + i*0x28, name) || !name[0] ||
                !String(*catalog + i*0x28+8, variant, true)) return false;
            entry.owned = std::any_of(records.begin(), records.end(), [&](const auto& r) {
                return r.owned && r.name == name && r.variant == variant;
            });
            if (entry.owned) { entry.name = name; entry.variant = variant; }
            out.entries[out.count++] = entry;
        }
        return Pointer(owner.save+0x838) == saved && Value<std::int32_t>(owner.save+0x830) == saved_count;
    }
    Memory& memory;
    std::size_t bytes{};
};
} // namespace SmoCompanion

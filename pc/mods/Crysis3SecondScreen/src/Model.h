#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <type_traits>

namespace c3 {
constexpr int Width = 1240, Height = 1080;
constexpr uint32_t Schema = 1;
constexpr uint64_t MaxAgeMs = 750;
enum class Page : uint8_t { Map, Weapons, Suit, Objectives, Status, Count };
enum class Phase : uint8_t { Unverified, Loading, Gameplay, Menu, Paused, Cutscene, Dead, Stopped };
enum class Action : uint8_t { None, EquipWeapon, EquipAttachment, ToggleCloak, ToggleArmor, ToggleModule, BuyModule };
template<class T> struct Value { T value{}; bool known{}; };
using Number = Value<float>;
using Integer = Value<int32_t>;
using Key = uint64_t; // Opaque generation-scoped handles, NEVER game addresses.
struct Text {
    std::array<char, 192> bytes{};
    std::string str() const { return {bytes.data(), std::find(bytes.begin(), bytes.end(), '\0')}; }
};
template<class T, size_t N> struct List {
    std::array<T, N> entries{};
    uint32_t count{};
    bool known{}, complete{};
    size_t size() const { return known ? std::min<size_t>(count, N) : 0; }
};
struct Capability {
    Key key{};
    Action action{};
    bool verified{}, allowed{};
};
struct Attachment {
    Text name{}; Value<bool> equipped{}, owned{}, nativeEnabled{}, defaultEntry{};
    Capability select{};
};
struct Weapon {
    Key key{}; Text name{}, category{}, icon{}; Integer magazine{}, reserve{}, capacity{};
    Value<bool> equipped{};
    List<Attachment, 12> attachments{};
    Capability select{};
};
struct Module {
    Key key{}; Text name{}, description{}, category{}, icon{};
    Integer cost{}; Number progress{}, target{};
    Value<bool> purchased{}, unlocked{}, installed{}, maximum{};
    Capability toggle{}, purchase{};
};
enum class ObjectiveState : uint8_t { Unknown, Active, Complete, Failed };
struct Objective { Key key{}; Text title{}, description{}; ObjectiveState state{}; Value<bool> optional{}; };
struct Resource { Key key{}; Text name{}, category{}; Integer quantity{}, capacity{}; };
struct Statistic { Text name{}, scope{}, unit{}; Number value{}; };
enum class MarkerKind : uint8_t { Objective, Enemy, Intel, PointOfInterest };
struct Marker { Key key{}; float x{}, y{}; MarkerKind kind{}; bool visible{}; };
struct Map {
    Key artwork{}; // Only verified asset catalog IDs; no touch-supplied paths.
    bool calibrated{};
    Number playerX{}, playerY{}, facing{}; // Normalized map coordinates; radians.
    List<Marker, 256> markers{};
};
struct Snapshot {
    uint32_t schema{Schema};
    uint64_t generation{}, sequence{}, sampledAt{};
    Phase phase{Phase::Unverified};
    bool adapterVerified{};
    bool readOnlyTrial{}; // Explicit development opt-in; never authorizes actions.
    Text mission{}, location{};
    Number health{}, maxHealth{}, energy{}, maxEnergy{};
    Value<bool> armor{}, cloak{};
    Capability toggleArmor{}, toggleCloak{};
    Integer upgradePoints{};
    List<Weapon, 8> weapons{};
    List<Module, 16> modules{};
    List<Objective, 64> objectives{};
    List<Resource, 48> resources{};
    List<Statistic, 48> statistics{};
    Map map{};
};
inline bool Current(const Snapshot& s, uint64_t now) {
    return s.schema == Schema && (s.adapterVerified || s.readOnlyTrial) && s.generation && s.sequence &&
        s.sampledAt <= now && now - s.sampledAt <= MaxAgeMs &&
        (s.phase == Phase::Gameplay || s.phase == Phase::Paused);
}
inline bool CanAct(const Snapshot& s, const Capability& c, uint64_t now) {
    return Current(s, now) && !s.readOnlyTrial && s.phase == Phase::Gameplay && c.key &&
        c.verified && c.allowed && c.action != Action::None;
}
struct Command { uint64_t request{}, generation{}, sequence{}, issuedAt{}; Capability capability{}; };
inline bool HasCapability(const Snapshot& s,const Capability& c) {
    auto same=[&](const Capability& owned) { return owned.key==c.key && owned.action==c.action && owned.verified && owned.allowed; };
    if (same(s.toggleArmor) || same(s.toggleCloak)) return true;
    for (size_t i=0;i<s.weapons.size();++i) {
        const auto& w=s.weapons.entries[i];
        if (same(w.select)) return true;
        for(size_t j=0;j<w.attachments.size();++j) if (same(w.attachments.entries[j].select)) return true;
    }
    for(size_t i=0;i<s.modules.size();++i) {
        const auto& m=s.modules.entries[i]; if(same(m.toggle) || same(m.purchase)) return true;
    }
    return false;
}
inline bool Eligible(const Snapshot& s, const Command& c, uint64_t now) {
    return c.request && c.generation == s.generation && c.sequence == s.sequence &&
        c.issuedAt <= now && now-c.issuedAt <= MaxAgeMs && CanAct(s, c.capability, now) && HasCapability(s,c.capability);
}
inline float Ratio(Number value, Number maximum) {
    if (!value.known || !maximum.known || !std::isfinite(value.value) ||
        !std::isfinite(maximum.value) || maximum.value <= 0 || value.value < 0) return -1;
    return std::clamp(value.value / maximum.value, 0.0f, 1.0f);
}
// Non-blocking producer boundary. Future game-thread adapter owns generation,
// validates handles anew, and publishes value copies; UI never walks the engine.
class Mailbox {
    std::mutex mutex_;
    Snapshot snapshot_{};
    std::atomic<Page> requestedPage_{Page::Status};
public:
    void RequestPage(Page page) noexcept {
        if(page<Page::Count) requestedPage_.store(page,std::memory_order_relaxed);
    }
    Page RequestedPage() const noexcept { return requestedPage_.load(std::memory_order_relaxed); }
    bool Publish(const Snapshot& s) {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock || s.schema != Schema || s.generation < snapshot_.generation ||
            (s.generation == snapshot_.generation && s.sequence <= snapshot_.sequence)) return false;
        snapshot_ = s; return true;
    }
    bool Read(Snapshot& s) {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock) return false;
        s = snapshot_; return true;
    }
};
static_assert(std::is_trivially_copyable_v<Snapshot>);
static_assert(sizeof(Snapshot) < 128 * 1024);
} // namespace c3

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dishonored::bridge {

// Internal payload boundary, following Skyrim's bounded snapshot/command design.
// No engine pointers, std::string storage, or engine-owned buffers cross it.
constexpr uint32_t Version = 0x00010006; // Verified live HUD targets retain world coordinates for map projection.
constexpr uint32_t CanvasWidth = 1240, CanvasHeight = 1080;
constexpr uint32_t FreshnessMilliseconds = 750;
enum class Page : uint8_t { Map, Powers, Equipment, Objectives, Journal, BoneCharms, Statistics, Count };
enum class Availability : uint8_t { Unresolved, WaitingForPlayer, Available, Incomplete, Unavailable };
enum class Lifecycle : uint8_t { Starting, Live, NoPlayer, UnsupportedPlayer, Stopping };

// Unknown and known-zero are different. A failed read must never become a
// plausible ammo quantity, completed objective, chaos value, or discovery flag.
template<class T> struct Value {
    T value{};
    bool known{};
};
using Integer = Value<int32_t>;
using Number = Value<float>;

// Opaque adapter-issued key. Only the game-thread adapter knows its meaning.
// It is scoped to a world generation and never used as an address by the UI.
using Key = uint64_t;
struct TextRef { uint32_t offset{}, length{}; }; // UTF-16 code units, excluding NUL.
struct AssetRef { uint64_t id{}; }; // Verified resource catalog entry, never a path from touch input.
struct Label { TextRef title{}, description{}; AssetRef artwork{}; };
struct Domain {
    Availability availability{Availability::Unresolved};
    uint64_t revision{};
    uint32_t total{}, stored{};
};
template<class T, std::size_t N> struct Collection {
    Domain domain{};
    std::array<T, N> entries{};
};

struct Power {
    Key key{};
    Label label{};
    Value<bool> unlocked{}, selected{}, selectable{};
    Integer tier{}, maximumTier{}, runeRequirement{}, manaCost{};
    Integer baseManaCost{}; // Definition value; never presented as modified consumption.
    bool passive{};
};
enum class EquipmentKind : uint8_t { Unknown, Weapon, Ammunition, Gadget, Consumable, Key, Resource, Upgrade };
struct Equipment {
    Key key{};
    Label label{};
    EquipmentKind kind{};
    Integer quantity{}, capacity{}, upgradeLevel{};
    Value<bool> selected{}, selectable{};
    Key weapon{}; // Ammunition's owned weapon key; never an engine pointer.
    // Modified weapon attributes, before attack-context and target modifiers.
    Number damage{}, headshotMultiplier{};
    bool rangedDamage{};
};
enum class ObjectiveState : uint8_t { Unknown, Active, Completed, Failed };
enum class TargetState : uint8_t { Eliminated, Neutralized, Rescued, Spared, Active };
struct ChapterTarget { Label label{}; Value<TargetState> state{}; };
struct Objective {
    Key key{}, parent{};
    Label label{};
    TextRef status{};
    ObjectiveState state{};
    Value<bool> optional{}, tracked{};
    Integer progress{}, target{};
};
struct ObjectiveTarget {
    Key task{};
    uint64_t observedAt{};
    Label label{};
    Integer distanceMetres{}; // Current native marker-render input, not its cached label distance.
    Number worldX{}, worldY{}, worldZ{}; // Only current HUD-owned, visible, registry-validated targets.
    bool optional{};
};
enum class JournalKind : uint8_t { Unknown, Note, Book, Letter, Audiograph, MissionDocument, Clue, Key, Map };
struct JournalEntry {
    Key key{};
    Label label{};
    JournalKind kind{};
    TextRef contents{};
    bool contentsComplete{};
    Value<bool> playing{}, canPlay{}, canStop{};
};
struct BoneCharm {
    Key key{};
    Label label{};
    Integer tier{}, equippedSlot{};
    Value<bool> equipped{}, canEquip{}, canRemove{};
};
enum class StatisticScope : uint8_t { Unknown, CurrentMission, Campaign, SavedMission };
enum class StatisticFormat : uint8_t { Number, Check, Chaos };
enum class ChaosLevel : uint8_t { Low, High };
struct Statistic {
    Key key{};
    Label label{};
    StatisticScope scope{};
    StatisticFormat format{};
    TextRef context{};
    Value<bool> condition{};
    Number value{}, maximum{};
};
enum class MarkerKind : uint8_t { Location, PrimaryObjective, OptionalObjective, Entrance, Exit, Rune, BoneCharm, Security };
struct MapMarker {
    Key key{};
    Label label{};
    MarkerKind kind{};
    float x{}, y{}; // Normalized coordinates in this acquired map's artwork.
};
struct MapLegend { uint8_t symbol{}; TextRef description{}; }; // Native DisUIMapLegendItemType.
struct MapSheet {
    Key key{};
    TextRef title{};
    AssetRef artwork{};
    Collection<MapLegend, 35> legend{};
    Number playerX{}, playerY{}, facingRadians{};
    bool positionApproximate{};
};
struct Map {
    Domain domain{};
    Key key{};
    Label label{};
    Collection<MapSheet, 13> sheets{}; // Distinct, currently loaded and natively acquired maps only.
    bool followingPlayer{true};
    // These are populated together only after discovery and world-to-map
    // calibration have been established for the actual acquired map.
    Value<bool> acquired{};
    Number playerX{}, playerY{}, facingRadians{};
    bool positionApproximate{};
    Collection<MapMarker, 256> markers{};
    Collection<MapLegend, 35> legend{};
};

// Long documents use an arena instead of per-entry tiny text arrays. On arena
// exhaustion, publish Incomplete and require a separate document request; do
// not present a truncated fragment as the complete readable contents.
constexpr std::size_t TextCapacity = 262144;
// 19 live records plus nine native saved missions, each with up to sixteen
// numeric values and sixteen independently filtered narrative summary rows.
constexpr std::size_t StatisticCapacity = 19 + 9 * (16 + 16);
struct Snapshot {
    uint32_t version{Version}, size{sizeof(Snapshot)};
    uint64_t sequence{}, generation{}, sampledAt{};
    Lifecycle lifecycle{Lifecycle::Starting};
    Integer health{}, maximumHealth{}, mana{}, maximumMana{}, coins{}, runes{}, charmSlots{};
    std::array<Value<Key>, 10> equippedCharms{}; // Known zero is an empty native slot.
    Value<ChaosLevel> chaos{};
    TextRef mission{}, area{};
    Label chapter{};
    std::array<ChapterTarget, 2> chapterTargets{};
    Collection<Power, 64> powers{};
    Collection<Equipment, 256> equipment{};
    Collection<Objective, 256> objectives{};
    Collection<ObjectiveTarget, 256> objectiveTargets{};
    Collection<JournalEntry, 1024> journal{};
    Collection<BoneCharm, 256> charms{};
    Collection<Statistic, StatisticCapacity> statistics{};
    Map map{};
    uint32_t textUsed{1}; // Offset zero is the empty string.
    std::array<char16_t, TextCapacity> text{};
};

enum class CommandKind : uint8_t { SelectPower, SelectEquipment, EquipCharm, RemoveCharm, ReadDocument, UseConsumable, PlayAudiograph, StopAudiograph, SelectMap };
struct Command {
    uint32_t version{Version};
    uint64_t request{}, generation{}, domainRevision{}, issuedAt{};
    CommandKind kind{};
    Key key{};
};
enum class CommandResult : uint8_t {
    Applied, RejectedStale, RejectedUnavailable, RejectedOwnership,
    RejectedEligibility, RejectedBusy, FailedValidation, Unsupported
};
struct Acknowledgement {
    uint64_t request{}, generation{};
    CommandResult result{CommandResult::Unsupported};
};

inline bool Fresh(const Snapshot& snapshot, uint64_t now) noexcept {
    return snapshot.version == Version && snapshot.size == sizeof(Snapshot) &&
        snapshot.lifecycle == Lifecycle::Live && snapshot.sampledAt <= now &&
        now - snapshot.sampledAt <= FreshnessMilliseconds;
}
inline bool ValidText(const Snapshot& snapshot, TextRef ref) noexcept {
    return snapshot.textUsed <= TextCapacity && ref.offset < snapshot.textUsed &&
        ref.length < snapshot.textUsed - ref.offset &&
        snapshot.text[ref.offset + ref.length] == u'\0';
}
static_assert(std::is_trivially_copyable_v<Snapshot>);
static_assert(std::is_trivially_copyable_v<Command>);
static_assert(sizeof(Snapshot) < 1024 * 1024, "Keep the hand-off and UI memory bounded");

} // namespace dishonored::bridge

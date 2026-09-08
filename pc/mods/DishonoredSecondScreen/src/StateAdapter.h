#pragma once

#include "GameMemory.h"
#include "Mailbox.h"
#include "Definitions.h"
#include <memory>

namespace dishonored {

// Owned exclusively by the verified game thread. Construct on the bootstrap
// worker before enabling hooks; destruction must follow hook quiescence.
class StateAdapter {
public:
    StateAdapter(Address image, Bridge& bridge, const Definitions* definitions = nullptr, bool nativeActions = false);
    void Tick(Address controller, uint64_t now) noexcept;
    void Invalidate(bridge::Lifecycle reason, uint64_t now) noexcept;
    // Established game thread only, after the original task marker render.
    // This stores bounded observations; object validation happens in Tick.
    void ObserveTaskMarker(Address marker, Address actor, int32_t index, float distance,
        uint32_t flags, uint64_t now) noexcept;
    void ObserveMapBoard(Address actor, Address level, int32_t index) noexcept;
private:
    bool Roots(Address controller, Address& pawn, Address* world = nullptr) noexcept;
    void ReadDiagnostics(uint64_t now) noexcept;
    void RootDiagnostics(Address controller, uint64_t now) noexcept;
    bool FieldAt(Address object, std::u16string_view name, std::u16string_view kind,
        uint32_t bytes, Address& address) noexcept;
    bool Text(Address string, bridge::Snapshot& snapshot, bridge::TextRef& out) noexcept;
    bridge::AssetRef ItemIcon(Address definition) noexcept;
    bridge::Key Key(Address object, bridge::Page page) noexcept;
    void Scalars(Address pawn, bridge::Snapshot& snapshot) noexcept;
    bool PlayerTweaks(Address pawn, Address& tweaks) noexcept;
    bool GameInfo(Address& game) noexcept;
    void Chaos(Address pawn, bridge::Snapshot& snapshot) noexcept;
    void Currencies(Address pawn, bridge::Snapshot& snapshot) noexcept;
    void BoneCharms(Address pawn, bridge::Snapshot& snapshot) noexcept;
    bool CharmSlots(Address pawn, Address tweaks, int32_t& count) noexcept;
    void Powers(Address pawn, bridge::Snapshot& snapshot) noexcept;
    bool SelectedPower(Address pawn, int32_t& index) noexcept;
    void Objectives(Address controller, bridge::Snapshot& snapshot) noexcept;
    bool CurrentChapter(Address& chapter, bridge::Snapshot& snapshot, bool& detailsComplete) noexcept;
    bool ChapterTargets(Address game, Address chapter, bridge::Snapshot& snapshot) noexcept;
    bool ObjectiveTasks(Address objective, bridge::Key parent, bool objectiveActive, bool objectiveTracked, bool optional,
        bridge::Snapshot& snapshot, uint64_t& hash) noexcept;
    void ObjectiveMarkers(bridge::Snapshot& snapshot) noexcept;
    bool ObjectiveTaskTargets(Address task, bridge::Key parent, bridge::Snapshot& snapshot, uint64_t& hash) noexcept;
    void Maps(Address pawn, bridge::Snapshot& snapshot, uint64_t now) noexcept;
    void CurrentArea(bridge::Snapshot& snapshot) noexcept;
    bool MapDefinitions(Address& definitions) noexcept;
    bool AcquiredMap(Address actor, Address level, int32_t index, uint8_t& map) noexcept;
    bridge::CommandResult SelectMap(bridge::Key key, const bridge::Snapshot& snapshot) noexcept;
    bool AbstractInventory(Address pawn, bridge::Snapshot& snapshot) noexcept;
    void Equipment(Address pawn, bridge::Snapshot& snapshot) noexcept;
    bool Weapons(Address pawn, bridge::Snapshot& snapshot) noexcept;
    void WeaponAttributes(Address item, bridge::Equipment& entry) noexcept;
    bool KeyRing(Address pawn, bridge::Snapshot& snapshot) noexcept;
    bool WeaponDefinition(Address item, Address& tweaks, Address& player) noexcept;
    bool SelectedItem(Address inventory, uint8_t usage, Address& item) noexcept;
    struct AmmoBinding { Address item{}; bridge::Key key{}; uint8_t usage{}, current{}; bool selected{}, known{}; };
    bool AmmunitionBindings(Address pawn, std::array<AmmoBinding, 12>& bindings) noexcept;
    void Journal(Address pawn, bridge::Snapshot& snapshot) noexcept;
    bool JournalPart(Address item, bridge::Snapshot& snapshot, bool& anyText) noexcept;
    uint64_t JournalGroupSignature(uint32_t entry) noexcept;
    bridge::CommandResult ReadDocument(bridge::Key key, const bridge::Snapshot& snapshot) noexcept;
    bool CopyNativeDocumentPart(Address item, uint32_t& length) noexcept;
    bool AudioLogContext(Address* actorOut = nullptr, Address* componentOut = nullptr) noexcept;
    bridge::Value<bool> AudioLogPlaying(Address item) noexcept;
    void AudioLogState(Address item, bridge::JournalEntry& entry) noexcept;
    bridge::CommandResult AudioLogAction(const bridge::Command& command, const bridge::Snapshot& snapshot) noexcept;
    void MissionStatistics(Address pawn, bridge::Snapshot& snapshot) noexcept;
    bool SavedStatistics(bridge::Snapshot& snapshot) noexcept;
    bool SavedStatisticsDefinitions(Address game, Address& tweaks) noexcept;
    bool Literal(std::u16string_view text, bridge::Snapshot& snapshot, bridge::TextRef& out) noexcept;
    void Commands(const bridge::Snapshot& snapshot, uint64_t now) noexcept;
    bridge::Value<bool> NativeInput(uint32_t mask) noexcept;
    bridge::CommandResult Apply(const bridge::Command& command, const bridge::Snapshot& snapshot, uint64_t now) noexcept;
    bool PendingApplied(const bridge::Snapshot& snapshot) const noexcept;
    bridge::CommandResult SelectAmmunition(const bridge::Command& command, uint64_t now) noexcept;
    void Diagnostics(const bridge::Snapshot& snapshot, uint64_t now) noexcept;
    uint64_t Revision(uint32_t domain, uint64_t hash) noexcept;
    Memory memory_;
    std::unique_ptr<Memory> cleanupMemory_; // Reserved read budget for restoring native transcript scratch state.
    Reflection reflection_;
    Bridge& bridge_;
    const Definitions* definitions_{};
    bool nativeActions_{};
    bool keyRingComplete_{}; // Refreshed by Equipment before Journal each sample.
    struct Pending { bridge::Command command{}; uint64_t started{}; bool active{}, awaitingAmmoWeapon{}; } pending_;
    Address pawn_{}, controller_{}, world_{};
    uint64_t generation_{}, sequence_{}, lastSample_{}, lastTick_{};
    bridge::Lifecycle lifecycle_{bridge::Lifecycle::Starting};
    std::array<uint64_t, 7> loggedRevisions_{};
    std::array<int32_t, 4> loggedScalars_{};
    uint8_t loggedScalarMask_{};
    uint64_t loggedGeneration_{};
    enum class RootStage : uint8_t { Ready, Quiescence, ControllerIdentity, ControllerActor, PawnLink, PawnClass, PawnActor, PawnVtable, WorldIdentity };
    RootStage rootStage_{};
    uint64_t nextReadLog_{}, nextFailureLog_{}, nextRootLog_{};
    uint32_t suppressedFailures_{};
    std::array<uint64_t, 7> hashes_{}, revisions_{};
    std::unique_ptr<char16_t[]> stringBuffer_;
    struct JournalWorkspace {
        struct Record { Address item; int32_t quantity; uint32_t flags; };
        struct Group { Address item{}; uint32_t entry{}; };
        int32_t recordCount{};
        uint32_t entryCount{};
        bool valid{};
        std::array<Record, 16384> records{};
        std::array<Address, 16384> parents{};
        std::array<int32_t, 16384> next{};
        std::array<int32_t, 1024> roots{}, heads{}, tails{};
        std::array<Group, 2048> groups{};
    };
    std::unique_ptr<JournalWorkspace> journal_;
    struct DocumentCache {
        bridge::Key key{};
        uint64_t generation{}, signature{};
        uint32_t length{};
        std::array<char16_t, bridge::TextCapacity> text{};
    };
    std::unique_ptr<DocumentCache> document_;
    struct MarkerObservation {
        Address marker{}, actor{};
        int32_t index{};
        float distance{};
        uint32_t flags{};
        uint64_t at{};
    };
    std::array<MarkerObservation, 256> taskMarkers_{};
    std::array<MarkerObservation, 256> ownedTaskMarkers_{};
    uint32_t ownedTaskMarkerCount_{};
    bool taskMarkersKnown_{};
    uint32_t markerObservations_{};
    struct MapHint { Address actor{}, level{}; int32_t index{}; };
    std::array<MapHint, 32> mapHints_{};
    uint32_t mapHintCount_{}, mapHintNext_{};
    struct MapWorkspace {
        struct Candidate { Address actor{}, level{}; int32_t index{}; bool seen{}; };
        std::array<Candidate, 256> candidates{};
        std::array<Address, 128> loadedLevels{};
        uint32_t count{}, levelIndex{}, actorIndex{};
        uint64_t generation{}, nextScan{};
        Address world{}, levelsData{};
        int32_t levelsCount{};
        bool complete{}, scanFailed{};
        uint8_t preferredMap{}; // UI view preference, cleared with this world/generation.
    } maps_;
};

} // namespace dishonored

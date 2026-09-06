#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ac2ds {
constexpr int Width = 1240, Height = 1080;
enum class Page { Map, Equipment, Inventory, Missions, Database, Progress };
enum class Phase { Starting, Unsupported, Loading, Playing, Cutscene, Paused, Stopped };
enum Capability : uint32_t { Player = 1, Map = 2, Equipment = 4, Inventory = 8,
    Missions = 16, Database = 32, Progress = 64, Preview = 128 };
enum class ActionKind { Equip, Use, Select, Track, Replay };
constexpr uint32_t ActionBit(ActionKind kind) { return 1u << static_cast<unsigned>(kind); }
struct Image { int width{}, height{}; std::vector<uint32_t> bgra; };
struct Item {
    uint64_t id{}, revision{};
    std::wstring name, category, description, details;
    std::optional<unsigned> quantity, capacity;
    bool owned{}, equipped{};
    uint32_t actions{};
    std::shared_ptr<const Image> icon;
};
struct Marker {
    uint64_t id{};
    float x{}, y{}; // Normalized native map coordinates, after native discovery checks.
    std::wstring name, description;
    bool discovered{}, visible{};
};
struct Memory {
    uint64_t id{}, revision{};
    std::wstring sequence, name, description, objective;
    bool unlocked{}, complete{}, active{}, replayable{}, exposed{};
    std::optional<double> synchronization;
};
struct DatabaseEntry {
    uint64_t id{};
    std::wstring category, title, text;
    bool unlocked{};
    std::shared_ptr<const Image> illustration;
};
struct Statistic {
    std::wstring name;
    std::optional<double> value, maximum;
    bool percentage{};
};
struct Snapshot {
    uint64_t generation{}, revision{}, sampledAt{}, playerIdentity{}, worldIdentity{};
    Phase phase = Phase::Starting;
    uint32_t capabilities{};
    bool mapVisibilityVerified{};
    std::wstring location, objective, status = L"Establishing Animus connection";
    std::optional<double> health, maximumHealth, money, synchronization;
    std::optional<float> mapX, mapY, heading;
    std::shared_ptr<const Image> map, preview;
    std::vector<Marker> markers;
    std::vector<Item> items;
    std::vector<Memory> memories;
    std::vector<DatabaseEntry> database;
    std::vector<Statistic> statistics;
};
struct Command {
    uint64_t request{}, generation{}, revision{}, entity{}, entityRevision{}, enqueuedAt{};
    ActionKind kind{};
};

// No engine pointer crosses into the window thread. The game-thread adapter alone
// constructs snapshots and re-resolves command identities immediately before use.
class StateStore {
    mutable std::mutex mutex_;
    std::shared_ptr<const Snapshot> snapshot_ = std::make_shared<Snapshot>();
    std::deque<Command> commands_;
    uint64_t nextRequest_ = 1;
public:
    std::shared_ptr<const Snapshot> Read() const {
        std::lock_guard<std::mutex> guard(mutex_); return snapshot_;
    }
    bool Publish(Snapshot value) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (value.generation < snapshot_->generation ||
            (value.generation == snapshot_->generation && value.revision <= snapshot_->revision)) return false;
        if (value.items.size() > 2048 || value.memories.size() > 1024 ||
            value.database.size() > 4096 || value.markers.size() > 4096 || value.statistics.size() > 128) return false;
        value.markers.erase(std::remove_if(value.markers.begin(), value.markers.end(), [](const Marker& marker) {
            return !marker.discovered || !marker.visible || !std::isfinite(marker.x) || !std::isfinite(marker.y) ||
                marker.x < 0 || marker.x > 1 || marker.y < 0 || marker.y > 1;
        }), value.markers.end());
        if (!value.mapVisibilityVerified) { value.capabilities &= ~Map; value.map.reset(); value.markers.clear(); }
        if (!(value.capabilities & Map)) { value.map.reset(); value.markers.clear(); }
        if (!(value.capabilities & Inventory)) value.items.clear();
        if (!(value.capabilities & Missions)) value.memories.clear();
        if (!(value.capabilities & Database)) value.database.clear();
        if (!(value.capabilities & Progress)) { value.statistics.clear(); value.synchronization.reset(); }
        if (!(value.capabilities & Preview)) value.preview.reset();
        if (!(value.capabilities & Player)) {
            value.health.reset(); value.maximumHealth.reset(); value.money.reset();
            value.mapX.reset(); value.mapY.reset(); value.heading.reset();
        }
        if (value.generation != snapshot_->generation || value.phase != Phase::Playing) commands_.clear();
        // A loading snapshot cannot accidentally retain old content/pointers/art.
        if (value.phase == Phase::Starting || value.phase == Phase::Loading ||
            value.phase == Phase::Stopped || value.phase == Phase::Unsupported) {
            Snapshot empty;
            empty.phase = value.phase; empty.generation = value.generation;
            empty.revision = value.revision; empty.sampledAt = value.sampledAt;
            empty.status = std::move(value.status); value = std::move(empty);
        }
        snapshot_ = std::make_shared<const Snapshot>(std::move(value)); return true;
    }
    bool Queue(Command command, uint64_t now) {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto& s = *snapshot_;
        if (unsigned(command.kind) > unsigned(ActionKind::Replay) ||
            s.phase != Phase::Playing || command.generation != s.generation ||
            command.revision != s.revision || now < s.sampledAt || now - s.sampledAt > 750 ||
            commands_.size() >= 16) return false;
        bool allowed = false;
        if (command.kind == ActionKind::Replay || command.kind == ActionKind::Track) {
            for (const auto& memory : s.memories) if (memory.id == command.entity &&
                memory.revision == command.entityRevision && memory.unlocked &&
                (s.capabilities & Missions)) {
                allowed = command.kind == ActionKind::Replay && memory.replayable;
            }
        } else for (const auto& item : s.items) if (item.id == command.entity &&
            item.revision == command.entityRevision && item.owned &&
            (s.capabilities & Inventory) && (item.actions & ActionBit(command.kind))) allowed = true;
        if (!allowed) return false;
        for (const auto& pending : commands_) if (pending.entity == command.entity && pending.kind == command.kind) return false;
        command.request = nextRequest_++; command.enqueuedAt = now;
        commands_.push_back(command); return true;
    }
    std::optional<Command> Take(uint64_t now) {
        std::lock_guard<std::mutex> guard(mutex_);
        while (!commands_.empty()) {
            Command next = commands_.front(); commands_.pop_front();
            if (snapshot_->phase == Phase::Playing && next.generation == snapshot_->generation &&
                next.revision == snapshot_->revision && now >= next.enqueuedAt && now - next.enqueuedAt <= 500) return next;
        }
        return {};
    }
};
}

#include "StateAdapter.h"
#include "MapCatalog.h"
#include "MapCalibration.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dishonored {
void StateAdapter::ObserveMapBoard(Address actor, Address level, int32_t index) noexcept {
    // Called only on the established game thread, after the original note
    // interaction. This bounded hint is not itself evidence of discovery.
    if (!actor || !level || index < 0) return;
    for (uint32_t i = 0; i < mapHintCount_; ++i)
        if (mapHints_[i].actor == actor && mapHints_[i].index == index) return;
    mapHints_[mapHintNext_] = {actor, level, index};
    mapHintNext_ = (mapHintNext_ + 1) % uint32_t(mapHints_.size());
    mapHintCount_ = std::min(mapHintCount_ + 1, uint32_t(mapHints_.size()));
}

void StateAdapter::CurrentArea(bridge::Snapshot& snapshot) noexcept {
    if (!definitions_) return;
    Address world{}, info{}, field{}; GameArray streaming{};
    // World::GetMapName 0078d340: first LevelStreamingPersistent's PackageName
    // takes precedence over the World's outermost package. Do not use the
    // loading overlay's last title, which can describe a destination in transit.
    if (!memory_.Read(memory_.Image() + 0x103d878, world) || !memory_.IsA(world, u"World") ||
        !memory_.Read(world + 0x2c0, info) || !memory_.IsA(info, u"WorldInfo") ||
        !FieldAt(info, u"StreamingLevels", u"ArrayProperty", 12, field) || field != info + 0x314 ||
        !memory_.Array(field, streaming, 128, 4)) return;
    GameName name{}; bool found = false;
    for (int32_t i = 0; i < streaming.count; ++i) {
        Address level{}, type{};
        if (!memory_.Read(streaming.data + uint32_t(i) * 4, level)) return;
        if (!level) continue;
        if (!memory_.Object(level, type)) return;
        if (!memory_.IsA(level, u"LevelStreamingPersistent")) continue;
        // Native 0078d434 passes this exact embedded FName to ToString.
        if (!memory_.Read(level + 0x38, name)) return;
        found = true; break;
    }
    if (!found) {
        // GetOutermost 0041fc50 follows UObject.Outer (+24). Bound traversal
        // and validate registry membership, including the final package.
        Address object = world; std::array<Address, 32> visited{};
        for (uint32_t depth = 0; depth < visited.size(); ++depth) {
            Address outer{}, type{};
            if (std::find(visited.begin(), visited.begin() + depth, object) != visited.begin() + depth ||
                !memory_.Object(object, type) || !memory_.Read(object + 0x24, outer)) return;
            visited[depth] = object;
            if (!outer) {
                if (!memory_.IsA(object, u"Package") || !memory_.Read(object + layout::ObjectName, name)) return;
                found = true; break;
            }
            object = outer;
        }
    }
    std::array<char16_t, 128> package{}; uint32_t length{};
    if (!found || !memory_.Name(name, package.data(), uint32_t(package.size()), length)) return;
    const auto title = definitions_->MapName(std::u16string_view(package.data(), length));
    if (!title.empty()) Literal(title, snapshot, snapshot.area);
}

bool StateAdapter::MapDefinitions(Address& definitions) noexcept {
    Address game{}, manager{}, note{}, field{};
    definitions = 0;
    if (!GameInfo(game) ||
        !FieldAt(game, u"m_pGlobalUIManager", u"ObjectProperty", 4, field) || field != game + 0x41c ||
        !memory_.Read(field, manager)) return false;
    if (manager) {
        if (!memory_.IsA(manager, u"DisGlobalUIManager") ||
            !FieldAt(manager, u"m_pNote", u"ObjectProperty", 4, field) || field != manager + 0x2e8 ||
            !memory_.Read(field, note)) return false;
        if (note) {
            if (!memory_.IsA(note, u"DisGFxMoviePlayerNote") ||
                !FieldAt(note, u"m_pTweaks", u"ObjectProperty", 4, field) || field != note + 0x1b0 ||
                !memory_.Read(field, definitions)) return false;
            if (definitions) return memory_.IsA(definitions, u"DisTweaks_GFxMoviePlayerNote");
        }
    }
    // Native map getter 00bcca10 uses the note's tweak override, or this exact
    // class default when that override is absent. UClass::GetDefaultObject
    // 004967f0 reads +13c but may construct it if missing. Only read an already
    // loaded default here: maps must not require opening/constructing a menu.
    // Invalid nonnull pointers above are not treated as missing overrides.
    Address type{}, actual{};
    return memory_.Read(memory_.Image() + 0x10627b0, type) && memory_.IsA(type, u"Class") &&
        memory_.ObjectName(type, u"DisTweaks_GFxMoviePlayerNote") &&
        memory_.Read(type + 0x13c, definitions) && memory_.Object(definitions, actual) && actual == type;
}

bool StateAdapter::AcquiredMap(Address actor, Address level, int32_t index, uint8_t& map) noexcept {
    map = 0;
    Address table{}, field{}, tweaks{}; uint32_t used{};
    if (maps_.levelsCount < 0 || uint32_t(maps_.levelsCount) > maps_.loadedLevels.size() ||
        std::find(maps_.loadedLevels.begin(), maps_.loadedLevels.begin() + maps_.levelsCount, level) ==
            maps_.loadedLevels.begin() + maps_.levelsCount ||
        !memory_.Registered(actor, index) || !memory_.LiveActor(actor) ||
        !memory_.Read(actor, table) || table != memory_.Image() + 0xcd7b48) return false;
    // Native interaction 00a36050 sets +3c0 bit0. Custom save 00a223b0
    // and restore 00a31810 persist this flag, including map-board actors.
    return FieldAt(actor, u"m_bAlreadyUsed", u"BoolProperty", 4, field) && field == actor + 0x3c0 &&
        memory_.Read(field, used) && (used & 1) &&
        FieldAt(actor, u"m_pTweaks", u"ObjectProperty", 4, field) && memory_.Read(field, tweaks) &&
        memory_.IsA(tweaks, u"DisTweaks_AbstractItemPickupNote") &&
        FieldAt(tweaks, u"m_Map", u"ByteProperty", 1, field) && field == tweaks + 0x10d &&
        memory_.Read(field, map) && map > 0 && map < 14;
}

bridge::CommandResult StateAdapter::SelectMap(bridge::Key key, const bridge::Snapshot& snapshot) noexcept {
    using Result = bridge::CommandResult;
    if (key >= 14 || !snapshot.map.sheets.domain.stored ||
        snapshot.map.sheets.domain.stored > snapshot.map.sheets.entries.size()) return Result::RejectedOwnership;
    if (!key) { maps_.preferredMap = 0; return Result::Applied; }
    const auto end = snapshot.map.sheets.entries.begin() + snapshot.map.sheets.domain.stored;
    if (std::find_if(snapshot.map.sheets.entries.begin(), end,
        [key](const bridge::MapSheet& sheet) { return sheet.key == key; }) == end) return Result::RejectedOwnership;
    // Re-read current world membership, not only the most recent UI snapshot.
    // A level can have been removed in-place without changing its TArray data.
    Address world{}; GameArray levels{}; std::array<Address, 128> loaded{};
    if (maps_.generation != generation_ || !memory_.Read(memory_.Image() + 0x103d878, world) ||
        world != maps_.world || !memory_.IsA(world, u"World") ||
        !memory_.Array(world + 0x3c, levels, loaded.size(), 4) ||
        !memory_.Bytes(levels.data, loaded.data(), uint32_t(levels.count) * 4) ||
        loaded != maps_.loadedLevels || levels.count != maps_.levelsCount) return Result::RejectedStale;
    for (uint32_t i = 0; i < maps_.count; ++i) {
        const auto candidate = maps_.candidates[i]; uint8_t map{};
        if (AcquiredMap(candidate.actor, candidate.level, candidate.index, map) && map == key) {
            maps_.preferredMap = map; return Result::Applied;
        }
    }
    return memory_.Failed() ? Result::FailedValidation : Result::RejectedOwnership;
}

void StateAdapter::Maps(Address pawn, bridge::Snapshot& snapshot, uint64_t now) noexcept {
    // Actor iterator 0054ea30: GWorld+3c owns Level pointers, each Level+38
    // owns actor pointers. Amortize discovery over ticks, then revalidate only
    // known map-note candidates. No full object-registry scan per frame.
    Address world{}; GameArray levels{};
    std::array<Address, 128> loadedLevels{};
    if (!memory_.Read(memory_.Image() + 0x103d878, world) || !memory_.IsA(world, u"World") ||
        !memory_.Array(world + 0x3c, levels, loadedLevels.size(), 4) ||
        !memory_.Bytes(levels.data, loadedLevels.data(), uint32_t(levels.count) * 4)) return;
    if (maps_.generation != generation_ || maps_.world != world) {
        maps_ = {}; maps_.generation = generation_; maps_.world = world;
    }
    // Streaming can replace level pointers in-place without reallocating the
    // TArray or changing its length. Compare the actual loaded-level list.
    if (maps_.levelsData != levels.data || maps_.levelsCount != levels.count || maps_.loadedLevels != loadedLevels) {
        maps_.levelsData = levels.data; maps_.levelsCount = levels.count;
        maps_.loadedLevels = loadedLevels;
        maps_.levelIndex = maps_.actorIndex = 0; maps_.nextScan = 0; maps_.complete = false;
        const auto end = std::remove_if(maps_.candidates.begin(), maps_.candidates.begin() + maps_.count,
            [&](const MapWorkspace::Candidate& candidate) {
                return std::find(loadedLevels.begin(), loadedLevels.begin() + levels.count, candidate.level) == loadedLevels.begin() + levels.count;
            });
        maps_.count = uint32_t(end - maps_.candidates.begin());
    }
    if (now >= maps_.nextScan) {
        if (!maps_.levelIndex && !maps_.actorIndex) {
            maps_.scanFailed = false;
            for (uint32_t i = 0; i < maps_.count; ++i) maps_.candidates[i].seen = false;
        }
        uint32_t remaining = 128;
        while (remaining && maps_.levelIndex < uint32_t(levels.count)) {
            const Address level = loadedLevels[maps_.levelIndex];
            GameArray actors{}; uint32_t excluded{};
            if (!memory_.IsA(level, u"Level") || !memory_.Read(level + 0x2e8, excluded) ||
                !memory_.Array(level + 0x38, actors, 262144, 4)) {
                // A disappearing level must not stall discovery of every
                // later level. Retry it on the next bounded traversal.
                maps_.scanFailed = true; ++maps_.levelIndex; maps_.actorIndex = 0;
                --remaining; continue;
            }
            if (excluded || maps_.actorIndex >= uint32_t(actors.count)) {
                ++maps_.levelIndex; maps_.actorIndex = 0; continue;
            }
            std::array<Address, 32> batch{};
            const uint32_t count = std::min({remaining, uint32_t(batch.size()), uint32_t(actors.count) - maps_.actorIndex});
            if (!memory_.Bytes(actors.data + maps_.actorIndex * 4, batch.data(), count * 4)) { maps_.scanFailed = true; break; }
            maps_.actorIndex += count; remaining -= count;
            for (uint32_t i = 0; i < count; ++i) {
                Address table{}; int32_t index{};
                if (!batch[i] || !memory_.Read(batch[i], table) || table != memory_.Image() + 0xcd7b48 ||
                    !memory_.LiveActor(batch[i]) || !memory_.Read(batch[i] + 4, index)) continue;
                Address field{}, tweaks{}; uint8_t map{};
                if (!FieldAt(batch[i], u"m_pTweaks", u"ObjectProperty", 4, field) || !memory_.Read(field, tweaks) ||
                    !memory_.IsA(tweaks, u"DisTweaks_AbstractItemPickupNote") ||
                    !FieldAt(tweaks, u"m_Map", u"ByteProperty", 1, field) || field != tweaks + 0x10d ||
                    !memory_.Read(field, map) || !map || map >= 14) continue;
                bool found = false;
                for (uint32_t j = 0; j < maps_.count; ++j) if (maps_.candidates[j].actor == batch[i] && maps_.candidates[j].index == index) {
                    maps_.candidates[j].level = level; maps_.candidates[j].seen = true; found = true; break;
                }
                if (!found && maps_.count < maps_.candidates.size()) maps_.candidates[maps_.count++] = {batch[i], level, index, true};
            }
        }
        if (memory_.Failed()) maps_.scanFailed = true;
        if (maps_.levelIndex >= uint32_t(levels.count)) {
            // Only a successful full traversal can prove an old candidate is
            // absent. Read failures must never be interpreted as destruction.
            if (!maps_.scanFailed) {
                const auto end = std::remove_if(maps_.candidates.begin(), maps_.candidates.begin() + maps_.count,
                    [](const MapWorkspace::Candidate& candidate) { return !candidate.seen; });
                maps_.count = uint32_t(end - maps_.candidates.begin());
            }
            maps_.levelIndex = maps_.actorIndex = 0; maps_.nextScan = now + (maps_.scanFailed ? 1000 : 5000);
            maps_.complete = !maps_.scanFailed;
        }
    }
    // An actual interaction need not wait for a multi-tick actor traversal.
    // Revalidate hints against current loaded levels, registry identity and
    // the native acquired flag. Normal book/note interactions do not qualify.
    for (uint32_t i = 0; i < mapHintCount_; ++i) {
        const auto hint = mapHints_[i]; uint8_t map{};
        if (!AcquiredMap(hint.actor, hint.level, hint.index, map)) continue;
        const auto end = maps_.candidates.begin() + maps_.count;
        const auto found = std::find_if(maps_.candidates.begin(), end, [&](const MapWorkspace::Candidate& candidate) {
            return candidate.actor == hint.actor && candidate.index == hint.index;
        });
        if (found != end) { found->level = hint.level; found->seen = true; }
        else if (maps_.count < maps_.candidates.size()) maps_.candidates[maps_.count++] = {hint.actor, hint.level, hint.index, true};
        else continue;
        bridge_.Record({Bridge::EventKind::MapDiscovery, now, generation_, map, 0, hint.index});
    }
    mapHintCount_ = mapHintNext_ = 0;
    Address definitions{}, field{};
    if (!MapDefinitions(definitions)) return;
    struct Position { float x, y, z; } position{};
    if (!FieldAt(pawn, u"Location", u"StructProperty", sizeof(position), field) || !memory_.Read(field, position) ||
        !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return;
    std::array<int32_t, 3> rotation{};
    const bool rotationKnown = FieldAt(pawn, u"Rotation", u"StructProperty", sizeof(rotation), field) &&
        memory_.Bytes(field, rotation.data(), sizeof(rotation));
    // Native GetAxes 0040a810 -> 00402c30 uses Rotator[1] for yaw,
    // sine index (yaw>>2)&3fff, cosine offset 4000: 65536 units/turn.
    constexpr double radiansPerUnit = 6.28318530717958647692 / 65536.0;
    const double yaw = double(uint32_t(rotation[1]) & 0xfffcU) * radiansPerUnit;
    uint8_t chosenMap{};
    double nearest = std::numeric_limits<double>::infinity();
    std::array<bool, 14> acquired{};
    for (uint32_t i = 0; i < maps_.count; ++i) {
        const auto candidate = maps_.candidates[i];
        uint8_t map{};
        if (!AcquiredMap(candidate.actor, candidate.level, candidate.index, map)) continue;
        acquired[map] = true;
        if (maps_.preferredMap && map != maps_.preferredMap) continue;
        Position board{};
        if (!FieldAt(candidate.actor, u"Location", u"StructProperty", sizeof(board), field) || !memory_.Read(field, board)) continue;
        const double dx = double(position.x) - board.x, dy = double(position.y) - board.y, dz = double(position.z) - board.z;
        const double distance = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(distance)) continue;
        if (distance < nearest) {
            nearest = distance; chosenMap = map;
        }
    }
    auto& map = snapshot.map;
    map.followingPlayer = !maps_.preferredMap;
    if (!FieldAt(definitions, u"m_Maps", u"StructProperty", 14 * 0x24, field) || field != definitions + 0xb8) return;
    const uint32_t visible = uint32_t(std::count(acquired.begin(), acquired.end(), true));
    map.sheets.domain.total = visible;
    for (uint32_t i = 1; i < acquired.size(); ++i) if (acquired[i]) {
        auto& sheet = map.sheets.entries[map.sheets.domain.stored];
        sheet = {};
        const Address content = definitions + 0xb8 + i * 0x24;
        if (!Text(content, snapshot, sheet.title)) continue;
        uint32_t length{};
        if (!memory_.String(content + 0xc, stringBuffer_.get(), 65536, length)) continue;
        const auto asset = std::find_if(MapAssets.begin(), MapAssets.end(), [i](const MapAsset& entry) { return entry.native == i; });
        if (asset == MapAssets.end() || std::u16string_view(stringBuffer_.get(), length) != asset->object) continue;
        sheet.key = i; sheet.artwork.id = 2000 + i;
        // Called only for a natively acquired, currently loaded map with an
        // exact original artwork path. Unknown calibrations return no marker.
        const auto projection = ProjectAcquiredMap(uint8_t(i), position.x, position.y, yaw, rotationKnown, position.z);
        sheet.playerX = {projection.x, projection.positionKnown};
        sheet.playerY = {projection.y, projection.positionKnown};
        sheet.facingRadians = {projection.facing, projection.facingKnown};
        sheet.positionApproximate = projection.positionKnown;
        // 00bd4790 copies content+18: native symbol byte at +0 and localized
        // description FString at +4, 16-byte records. Copy each acquired
        // sheet once so browsing and legends do not depend on game ticks.
        GameArray legend{};
        if (memory_.Array(content + 0x18, legend, 256, 16)) {
            sheet.legend.domain.total = uint32_t(legend.count);
            bool complete = uint32_t(legend.count) <= sheet.legend.entries.size();
            for (int32_t j = 0; j < legend.count && uint32_t(j) < sheet.legend.entries.size(); ++j) {
                auto& entry = sheet.legend.entries[sheet.legend.domain.stored];
                if (!memory_.Read(legend.data + uint32_t(j) * 16, entry.symbol) || entry.symbol >= 35 ||
                    !Text(legend.data + uint32_t(j) * 16 + 4, snapshot, entry.description)) { complete = false; break; }
                ++sheet.legend.domain.stored;
            }
            sheet.legend.domain.availability = complete ? bridge::Availability::Available : bridge::Availability::Incomplete;
        }
        ++map.sheets.domain.stored;
        if (i == chosenMap) {
            map.key = i; map.acquired = {true, true};
            map.label.title = sheet.title; map.label.artwork = sheet.artwork;
            map.legend = sheet.legend;
            map.playerX = sheet.playerX; map.playerY = sheet.playerY; map.facingRadians = sheet.facingRadians;
            map.positionApproximate = sheet.positionApproximate;
        }
    }
    map.sheets.domain.availability = maps_.complete && map.sheets.domain.stored == visible && !memory_.Failed() ?
        bridge::Availability::Available : bridge::Availability::Incomplete;
    map.domain.availability = bridge::Availability::Incomplete;
    map.domain.total = visible; map.domain.stored = map.key ? 1 : 0;
    // Printed board symbols remain part of the original art. Calibrated Corvo
    // position is explicitly approximate; no undiscovered secret markers added.
    uint64_t hash = (uint64_t(chosenMap) << 32) ^ map.label.artwork.id ^ (uint64_t(visible) << 16) ^ uint64_t(maps_.complete);
    hash = (hash ^ maps_.preferredMap) * 1099511628211ull;
    for (uint32_t i = 0; i < map.sheets.domain.stored; ++i) {
        const auto& sheet = map.sheets.entries[i]; hash = (hash ^ sheet.key) * 1099511628211ull;
        hash = (hash ^ sheet.artwork.id) * 1099511628211ull;
        hash = (hash ^ uint64_t(sheet.legend.domain.availability)) * 1099511628211ull;
        hash = (hash ^ sheet.legend.domain.total) * 1099511628211ull;
        for (uint32_t k = 0; k < sheet.legend.domain.stored; ++k) {
            const auto& entry = sheet.legend.entries[k]; hash = (hash ^ entry.symbol) * 1099511628211ull;
            if (bridge::ValidText(snapshot, entry.description)) for (uint32_t j = 0; j < entry.description.length; ++j)
                hash = (hash ^ snapshot.text[entry.description.offset + j]) * 1099511628211ull;
        }
        if (bridge::ValidText(snapshot, sheet.title)) for (uint32_t j = 0; j < sheet.title.length; ++j)
            hash = (hash ^ snapshot.text[sheet.title.offset + j]) * 1099511628211ull;
    }
    hash = (hash ^ uint64_t(map.sheets.domain.availability)) * 1099511628211ull;
    for (uint32_t i = 0; i < map.legend.domain.stored; ++i) {
        const auto& entry = map.legend.entries[i]; hash = (hash ^ entry.symbol) * 1099511628211ull;
        if (bridge::ValidText(snapshot, entry.description)) for (uint32_t j = 0; j < entry.description.length; ++j)
            hash = (hash ^ snapshot.text[entry.description.offset + j]) * 1099511628211ull;
    }
    hash = (hash ^ uint64_t(map.legend.domain.availability)) * 1099511628211ull;
    map.domain.revision = Revision(uint32_t(bridge::Page::Map), hash);
    map.legend.domain.revision = map.domain.revision;
    map.sheets.domain.revision = map.domain.revision;
}
} // namespace dishonored

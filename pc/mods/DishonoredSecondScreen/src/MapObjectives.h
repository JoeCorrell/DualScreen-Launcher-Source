#pragma once
#include "Bridge.h"
#include "MapCalibration.h"

namespace dishonored {
// Pure projection of already validated live HUD observations. No object lookup,
// level-template enumeration, game command or discovery inference occurs here.
inline void ProjectMapObjectives(const bridge::Snapshot& snapshot, bridge::Map& map, uint64_t now) noexcept {
    map.markers = {};
    if (snapshot.lifecycle != bridge::Lifecycle::Live || !map.acquired.known || !map.acquired.value ||
        !map.key || map.key > 255 || map.label.artwork.id != 2000+map.key || !HasMapCalibration(uint8_t(map.key))) return;
    map.markers.domain.availability = snapshot.objectiveTargets.domain.availability;
    const auto& source = snapshot.objectiveTargets;
    if (source.domain.stored > source.entries.size()) {
        map.markers.domain.availability = bridge::Availability::Incomplete; return;
    }
    for (uint32_t i = 0; i < source.domain.stored; ++i) {
        const auto& target = source.entries[i];
        if (!target.task || !target.observedAt || target.observedAt > now || now-target.observedAt > bridge::FreshnessMilliseconds ||
            !target.worldX.known || !target.worldY.known || !target.label.title.length ||
            !bridge::ValidText(snapshot,target.label.title)) continue;
        const auto point = ProjectAcquiredMap(uint8_t(map.key),target.worldX.value,target.worldY.value,0,false,
            target.worldZ.known ? double(target.worldZ.value) : std::numeric_limits<double>::quiet_NaN());
        if (!point.positionKnown) continue;
        ++map.markers.domain.total;
        if (map.markers.domain.stored == map.markers.entries.size()) {
            map.markers.domain.availability = bridge::Availability::Incomplete; continue;
        }
        auto& marker = map.markers.entries[map.markers.domain.stored++];
        // Snapshot-local hit identity; never accepted as a native command key.
        marker.key = uint64_t(i)+1; marker.label = target.label;
        marker.kind = target.optional ? bridge::MarkerKind::OptionalObjective : bridge::MarkerKind::PrimaryObjective;
        marker.x = point.x; marker.y = point.y;
    }
}
// Panning, zooming and press animations can render many times between samples.
// Reuse copied markers during those frames; text references are valid only for
// the same snapshot sequence. Expiry remains independent of game-thread ticks.
class MapObjectiveProjectionCache {
public:
    void Project(const bridge::Snapshot& snapshot, bridge::Map& map, uint64_t now) noexcept {
        if(valid_ && generation_==snapshot.generation && sequence_==snapshot.sequence &&
            lifecycle_==snapshot.lifecycle && key_==map.key && artwork_==map.label.artwork.id &&
            acquiredKnown_==map.acquired.known && acquired_==map.acquired.value &&
            now>=computedAt_ && now<expiresAt_) {
            map.markers=markers_;return;
        }
        ProjectMapObjectives(snapshot,map,now);
        generation_=snapshot.generation;sequence_=snapshot.sequence;lifecycle_=snapshot.lifecycle;
        key_=map.key;artwork_=map.label.artwork.id;acquiredKnown_=map.acquired.known;acquired_=map.acquired.value;
        markers_=map.markers;computedAt_=now;expiresAt_=std::numeric_limits<uint64_t>::max();valid_=true;
        const auto& source=snapshot.objectiveTargets;
        if(source.domain.stored>source.entries.size())return;
        for(uint32_t i=0;i<source.domain.stored;++i) {
            const auto at=source.entries[i].observedAt;
            if(at>now) { expiresAt_=now;break; }
            if(at && now-at<=bridge::FreshnessMilliseconds) {
                const uint64_t expiry=at+bridge::FreshnessMilliseconds+1;
                if(expiry<expiresAt_)expiresAt_=expiry;
            }
        }
    }
private:
    decltype(bridge::Map{}.markers) markers_{};
    uint64_t generation_{},sequence_{},key_{},artwork_{},computedAt_{},expiresAt_{};
    bridge::Lifecycle lifecycle_{};
    bool valid_{},acquiredKnown_{},acquired_{};
};
}

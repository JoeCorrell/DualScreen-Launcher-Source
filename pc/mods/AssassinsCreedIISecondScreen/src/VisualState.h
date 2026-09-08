#pragma once
#include "State.h"
#include "MapLegend.h"
#include <tuple>

namespace ac2ds {
// Compare owned display data, never engine addresses or sampling timestamps.
// Fresh command revisions still reach TouchInput even when no pixels change.
template<class T, class Equal>
bool SameRows(const std::vector<T>& a, const std::vector<T>& b, Equal equal) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), equal);
}
inline bool SameVisualState(const Snapshot& a, const Snapshot& b, Page page, bool mapMotion=true) {
    if (std::tie(a.generation,a.phase,a.capabilities,a.location,a.status,a.health,a.maximumHealth,a.money) !=
        std::tie(b.generation,b.phase,b.capabilities,b.location,b.status,b.health,b.maximumHealth,b.money)) return false;
    switch (page) {
    case Page::Map:
        return std::tie(a.map,a.preview,a.objective,a.notoriety,a.mapMarkersVerified) == std::tie(b.map,b.preview,b.objective,b.notoriety,b.mapMarkersVerified) && SameMapLegend(a,b) &&
            (!mapMotion || std::tie(a.mapX,a.mapY,a.heading)==std::tie(b.mapX,b.mapY,b.heading)) &&
            (!mapMotion || SameRows(a.markers,b.markers,[](const Marker& x,const Marker& y) {
                return std::tie(x.id,x.x,x.y,x.name,x.description,x.discovered,x.visible,x.artworkId,x.categoryId) ==
                       std::tie(y.id,y.x,y.y,y.name,y.description,y.discovered,y.visible,y.artworkId,y.categoryId);
            }));
    case Page::Equipment:
    case Page::Inventory:
        return (page != Page::Equipment || a.preview == b.preview) &&
            SameRows(a.items,b.items,[](const Item& x,const Item& y) {
                return std::tie(x.id,x.name,x.category,x.description,x.details,x.quantity,x.capacity,x.weaponRatings,
                                x.owned,x.equipped,x.actions,x.icon,x.artworkId,x.thumbnailId) ==
                       std::tie(y.id,y.name,y.category,y.description,y.details,y.quantity,y.capacity,y.weaponRatings,
                                y.owned,y.equipped,y.actions,y.icon,y.artworkId,y.thumbnailId);
            });
    case Page::Missions:
        return a.objective == b.objective && a.synchronization == b.synchronization &&
            SameRows(a.memories,b.memories,[](const Memory& x,const Memory& y) {
                return std::tie(x.id,x.sequence,x.name,x.description,x.objective,x.unlocked,x.complete,x.active,
                                x.replayable,x.exposed,x.synchronization) ==
                       std::tie(y.id,y.sequence,y.name,y.description,y.objective,y.unlocked,y.complete,y.active,
                                y.replayable,y.exposed,y.synchronization);
            });
    case Page::Database:
        return SameRows(a.database,b.database,[](const DatabaseEntry& x,const DatabaseEntry& y) {
            return std::tie(x.id,x.category,x.title,x.text,x.unlocked,x.illustration,x.artworkId) ==
                   std::tie(y.id,y.category,y.title,y.text,y.unlocked,y.illustration,y.artworkId);
        });
    case Page::Progress:
        return a.synchronization == b.synchronization &&
            SameRows(a.statistics,b.statistics,[](const Statistic& x,const Statistic& y) {
                return std::tie(x.name,x.value,x.maximum,x.percentage) == std::tie(y.name,y.value,y.maximum,y.percentage);
            });
    }
    return false;
}
inline bool SameSelectedMapDetails(const Snapshot& a,const Snapshot& b,uint64_t selected) {
    auto details=[&](const Snapshot& s) {
        for(const auto& marker:s.markers) if(selected && marker.id==selected)
            return marker.name+L"\n\n"+marker.description;
        return s.objective.empty()?std::wstring(L"Awaiting synchronization"):s.objective;
    };
    return details(a)==details(b);
}
}

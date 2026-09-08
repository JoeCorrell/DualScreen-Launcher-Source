#pragma once
#include "State.h"
namespace ac2ds {
// MapIcon.as type0 -> Minimap_Icon_ezio_MapDesc -> authored texture in
// Animus_Map_Icons (2318d8a3). Assets are loaded from the user's own archive.
inline constexpr uint32_t PlayerMarkerArtwork=0x18f36fc3;
struct MapLegendRow { uint32_t id{},artwork{};size_t count{};std::wstring name; };
inline uint32_t MarkerCategory(const Marker& marker){return marker.categoryId?marker.categoryId:UINT32_MAX;}
inline float MapMarkerSize(const Marker& marker){
    // Original localized Guard/Archer categories and their shared native icon.
    // Keep the touch hit area unchanged while reducing map clutter.
    return marker.categoryId==276391 || marker.categoryId==276392 || marker.artworkId==0x5e5d558c?20.f:36.f;
}
inline bool MapMarkerVisible(const Marker& marker,uint32_t filter=0){
    // Exclude the minimap hiding-spot overlay from the companion city map,
    // including hit testing and legend totals. The artwork guard also covers
    // older recorded snapshots without the localization category identity.
    return marker.categoryId!=282904 && marker.artworkId!=0x5e5d5596 &&
        marker.visible && marker.discovered && std::isfinite(marker.x) && std::isfinite(marker.y) &&
        marker.x>=0 && marker.x<=1 && marker.y>=0 && marker.y<=1 && (!filter || MarkerCategory(marker)==filter);
}
inline std::vector<MapLegendRow> MapLegend(const Snapshot& s){
    std::vector<MapLegendRow> rows;
    if(!(s.capabilities&Map) || !s.map)return rows;
    for(const auto& category:s.mapCategories){
        if(!category.id || category.id==282904 || category.artwork==0x5e5d5596 || category.name.empty())continue;
        if(std::none_of(rows.begin(),rows.end(),[&](const MapLegendRow& r){return r.id==category.id;}))
            rows.push_back({category.id,category.artwork,0,category.name});
    }
    for(const auto& marker:s.markers){
        if(!MapMarkerVisible(marker))continue;
        auto id=MarkerCategory(marker);
        auto row=std::find_if(rows.begin(),rows.end(),[&](const MapLegendRow& r){return r.id==id;});
        if(row==rows.end())rows.push_back({id,marker.artworkId,1,marker.name.empty()?L"Markers":marker.name});
        else ++row->count;
    }
    std::sort(rows.begin(),rows.end(),[](const MapLegendRow& a,const MapLegendRow& b){return a.name<b.name;});
    return rows;
}
inline bool SameMapLegend(const Snapshot& a,const Snapshot& b){
    auto aa=MapLegend(a),bb=MapLegend(b);if(aa.size()!=bb.size())return false;
    for(size_t i=0;i<aa.size();++i)if(aa[i].id!=bb[i].id || aa[i].artwork!=bb[i].artwork ||
        aa[i].count!=bb[i].count || aa[i].name!=bb[i].name)return false;
    return true;
}
}

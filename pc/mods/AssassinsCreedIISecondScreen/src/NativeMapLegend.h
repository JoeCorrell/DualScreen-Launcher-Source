#pragma once
#include "NativeMapMarkers.h"
#include "State.h"
namespace ac2ds::native {
inline uint32_t MapCategoryId(uint32_t type){
    // InformationCityLegend groups destinations, targets, objects and allies.
    return MapMarkerTitle(type==3 || type==40 || type==41?2:type);
}
// Generic category names/icons from original InformationCityLegend layers. This legend
// does not expose undiscovered marker instances, coordinates or quest titles.
template<class Lookup> std::vector<MapCategory> MapCategoryCatalog(Lookup lookup){
    std::vector<MapCategory> result;
    constexpr std::array<std::pair<uint32_t,uint32_t>,23> layers{{
        {1,282666},{2,282667},{22,282668},{34,282669},{37,282670},{33,282671},
        {35,282673},{24,276444},{38,282674},{39,282675},{23,276448},
        {31,282661},{43,282662},{42,282663},{12,276441},{17,276442},
        {14,276439},{13,276440},{15,276364},{28,282679},{29,282680},{27,282681},{50,282733}}};
    for(auto layer:layers){
        auto id=MapCategoryId(layer.first),art=MapMarkerArtwork(layer.first);
        if(std::any_of(result.begin(),result.end(),[&](const MapCategory& row){return row.id==id;}))continue;
        auto name=lookup(layer.second);if(name && !name->empty())result.push_back({id,art,*name});
    }
    return result;
}
}

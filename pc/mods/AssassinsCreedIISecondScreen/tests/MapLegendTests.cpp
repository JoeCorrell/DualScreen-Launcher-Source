#include "MapLegend.h"
#include "NativeMapLegend.h"
#include <cassert>
#include <cstdio>
using namespace ac2ds;
int main(){
    for(uint32_t type:{20u,21u,60u}){Marker m;m.categoryId=native::MapMarkerTitle(type);
        m.artworkId=native::MapMarkerArtwork(type);assert(MapMarkerSize(m)==20.f);}
    for(uint32_t type:{1u,2u,12u,17u,24u}){Marker m;m.categoryId=native::MapMarkerTitle(type);
        m.artworkId=native::MapMarkerArtwork(type);assert(MapMarkerSize(m)==36.f);}
    Snapshot s;s.capabilities=Map;s.map=std::make_shared<Image>();
    for(unsigned i=0;i<8;++i){Marker marker;marker.id=i+1;marker.categoryId=i<3?12:17;
        marker.name=i<3?L"Blacksmiths":L"Doctors";marker.artworkId=10+i/3;
        marker.visible=marker.discovered=true;marker.x=marker.y=.5f;s.markers.push_back(marker);}
    auto rows=MapLegend(s);assert(rows.size()==2 && rows[0].id==12 && rows[0].count==3 && rows[1].count==5);
    auto full=s;full.mapCategories={{12,10,L"Blacksmiths"},{17,11,L"Doctors"},{25,12,L"Treasure"},{25,12,L"Duplicate"},{282904,0,L"Hiding spots"}};
    auto fullRows=MapLegend(full);assert(fullRows.size()==3 && fullRows.back().id==25 && fullRows.back().count==0);
    auto empty=full;empty.markers.clear();assert(MapLegend(empty).size()==3);
    auto catalog=native::MapCategoryCatalog([](uint32_t id){return std::optional<std::wstring>{std::to_wstring(id)};});
    assert(catalog.size()==23);
    assert(native::MapCategoryId(2)==native::MapCategoryId(3) && native::MapCategoryId(40)==native::MapCategoryId(41));
    for(const auto& row:catalog)assert(row.id!=282904 && row.artwork && !row.name.empty());
    assert(std::count_if(catalog.begin(),catalog.end(),[](const MapCategory& c){return c.id==276388;})==1);
    assert(native::MapCategoryCatalog([](uint32_t){return std::optional<std::wstring>{};}).empty());
    auto withHiding=s;auto hiding=s.markers.front();hiding.categoryId=282904;hiding.artworkId=0;
    withHiding.markers.push_back(hiding);assert(!MapMarkerVisible(hiding));
    assert(SameMapLegend(s,withHiding));
    hiding.categoryId=0;hiding.artworkId=0x5e5d5596;withHiding.markers.push_back(hiding);
    assert(!MapMarkerVisible(hiding) && SameMapLegend(s,withHiding));
    auto moved=s;moved.markers[0].x=.8f;assert(SameMapLegend(s,moved));
    moved.markers[0].visible=false;assert(!SameMapLegend(s,moved));
    assert(MapMarkerVisible(s.markers[0],12) && !MapMarkerVisible(s.markers[0],17));
    s.markers[0].visible=false;s.markers[1].discovered=false;s.markers[2].x=2;
    rows=MapLegend(s);assert(rows.size()==1 && rows[0].id==17 && rows[0].count==5);
    s.markers[3].x=std::nanf("");assert(MapLegend(s)[0].count==4);
    s.capabilities=0;assert(MapLegend(s).empty());s.capabilities=Map;s.map.reset();assert(MapLegend(s).empty());
    std::puts("Map legend: counts include only live visible discoveries, filters keep category identity, movement preserves cached sidebar");
}

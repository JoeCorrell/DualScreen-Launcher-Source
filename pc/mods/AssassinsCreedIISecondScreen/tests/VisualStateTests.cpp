#include "VisualState.h"
#include <cassert>
#include <iostream>
using namespace ac2ds;
int main() {
    Snapshot a; a.generation=1; a.phase=Phase::Playing;
    a.capabilities=Player|Map|Inventory|Equipment|Missions|Database|Progress;
    a.items.resize(1); a.markers.resize(1); a.memories.resize(1); a.database.resize(1); a.statistics.resize(1);
    auto b=a; b.revision=20; b.sampledAt=4000; b.items[0].revision=20;
    for(int i=0;i<6;++i) assert(SameVisualState(a,b,Page(i)));
    b.mapX=.4f; assert(!SameVisualState(a,b,Page::Map));
    assert(SameVisualState(a,b,Page::Map,false));
    assert(SameVisualState(a,b,Page::Equipment));
    b=a; b.health=19; for(int i=0;i<6;++i) assert(!SameVisualState(a,b,Page(i)));
    b=a;b.notoriety=25;assert(!SameVisualState(a,b,Page::Map,false));assert(SameVisualState(a,b,Page::Inventory));
    b=a;b.mapMarkersVerified=true;assert(!SameVisualState(a,b,Page::Map,false));
    b=a; b.generation++; for(int i=0;i<6;++i) assert(!SameVisualState(a,b,Page(i)));
    b=a; b.items[0].quantity=2; assert(!SameVisualState(a,b,Page::Inventory));
    assert(!SameVisualState(a,b,Page::Equipment)); assert(SameVisualState(a,b,Page::Map));
    b=a; b.items[0].weaponRatings=std::array<unsigned,3>{1,2,3}; assert(!SameVisualState(a,b,Page::Inventory));
    b=a; b.items[0].actions=ActionBit(ActionKind::Equip); assert(!SameVisualState(a,b,Page::Inventory));
    b=a; b.markers[0].visible=true; assert(!SameVisualState(a,b,Page::Map));
    assert(SameVisualState(a,b,Page::Map,false));
    b=a; b.markers[0].id=99;b.markers[0].name=L"Native marker";
    assert(!SameVisualState(a,b,Page::Map));assert(SameVisualState(a,b,Page::Map,false));
    assert(SameSelectedMapDetails(a,b,0));assert(!SameSelectedMapDetails(a,b,99));
    a=b;b.markers[0].x=.75f;assert(SameSelectedMapDetails(a,b,99));
    b.markers.clear();assert(!SameSelectedMapDetails(a,b,99));b=a;
    b=a; b.map=std::make_shared<Image>(); assert(!SameVisualState(a,b,Page::Map));
    assert(!SameVisualState(a,b,Page::Map,false));
    b=a; b.preview=std::make_shared<Image>(); assert(!SameVisualState(a,b,Page::Map));
    assert(!SameVisualState(a,b,Page::Equipment));
    b=a; b.memories[0].complete=true; assert(!SameVisualState(a,b,Page::Missions));
    b=a; b.database[0].unlocked=true; assert(!SameVisualState(a,b,Page::Database));
    b=a; b.statistics[0].value=2; assert(!SameVisualState(a,b,Page::Progress));
    b=a; b.items.clear(); assert(!SameVisualState(a,b,Page::Inventory));
    std::cout << "Visible state changes invalidate frames; sampling-only changes do not\n";
}

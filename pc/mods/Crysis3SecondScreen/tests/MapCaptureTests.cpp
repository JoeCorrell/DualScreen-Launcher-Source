#include "../src/MapCapture.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <memory>

int main() {
    auto m=std::make_unique<c3::MapCapture>();
    m->Invalidate(1,7);
    assert(m->SetBackground(1,7,"Minimap/verified-name-only.gfx",{90,true}));
    m->ClearAll(1,7);
    assert(m->Background(1,7).rotation.value==90);
    assert(m->SetBackground(1,7,""));
    assert(!m->Background(1,7).path.str().empty());
    assert(m->AddIcon(1,7,105,203,45,100,"player"));
    assert(m->AddObjective(1,7,20,30,100,"Primary A","Detail A",false,0,false));
    assert(m->AddObjective(1,7,25,35,100,"Secondary hidden","Detail",true,2,true));
    assert(m->AddObjective(1,7,40,50,100,"Primary B","Detail B",false,2,true));
    assert(!m->Icons(1,7,1000).known && !m->Objectives(1,7,1000).known);
    assert(m->UpdateIcons(1,7,1000));
    assert(m->Icons(1,7,1000).count==3 && !m->Objectives(1,7,1000).known);
    assert(!m->UpdateIcons(1,7,1200)); // not a fresh sample merely because repainted
    assert(m->UpdateObjectives(1,7,1200));
    const auto o=m->Objectives(1,7,1200);
    assert(o.count==2 && o.entries[0].message.str()=="Primary B");
    assert(o.entries[0].rawStatus==2 && o.entries[0].iconSlot==2);
    assert(o.entries[1].iconSlot==1 && !o.entries[1].showOnRadar);
    assert(m->Icons(1,7,1200).entries[2].frame.str()=="done");
    assert(!m->Icons(1,7,1751).known); // updateObjectives did not rejuvenate icons
    assert(m->Objectives(1,7,1751).known);
    assert(!m->UpdateObjectives(1,7,1800));
    assert(!m->Icons(2,7,1200).known && !m->Objectives(1,8,1200).known);
    m->ClearAll(2,7);
    assert(m->Background(2,7).path.str().empty());
    assert(!m->AddIcon(2,7,std::numeric_limits<float>::quiet_NaN(),0,0,100,"enemy"));
    assert(!m->UpdateIcons(2,7,2000));
    m->ClearAll(2,7);
    for(int i=0;i<32;++i) assert(m->AddIcon(2,7,float(i),0,0,100,"unknown-frame"));
    assert(!m->AddIcon(2,7,33,0,0,100,"enemy"));
    assert(!m->UpdateIcons(2,7,2000)); // no falsely complete/truncated transaction
    m->ClearAll(2,7);
    assert(!m->AddIcon(2,8,0,0,0,100,"enemy"));
    assert(!m->UpdateObjectives(2,7,2000));
    std::cout<<"PASS: map movie transactions, raw status/order, secondary filter, ownership, freshness, bounded coordinates\n";
}

#include "MissionNavigation.h"
#include <cassert>
#include <iostream>
using namespace ac2ds;
int main(){
    Snapshot s;s.capabilities=Missions;
    Memory first;first.id=(uint64_t(1)<<32)|1;first.sequence=L"Same localized title";
    first.unlocked=true;first.complete=true;s.memories.push_back(first);
    auto active=first;active.id=(uint64_t(2)<<32)|1;active.complete=false;active.active=true;s.memories.push_back(active);
    auto locked=first;locked.id=(uint64_t(1)<<32)|2;locked.unlocked=false;locked.complete=false;locked.exposed=true;s.memories.push_back(locked);
    auto hidden=first;hidden.id=(uint64_t(3)<<32)|1;hidden.unlocked=false;hidden.exposed=false;s.memories.push_back(hidden);
    auto side=active;side.id=(uint64_t(0x80000002)<<32)|1;s.memories.push_back(side);
    auto groups=MissionSequences(s);
    assert(groups.size()==3 && groups[0].id==1 && groups[1].id==2 && groups[2].id==0x80000002);
    assert(groups[0].visible==2 && groups[0].completed==1 && !groups[0].active);
    assert(groups[1].active && groups[2].active);
    auto rows=SequenceMemories(s,1);assert(rows.size()==2 && rows[0]->id==first.id && rows[1]->id==locked.id);
    assert(SequenceMemories(s,3).empty());
    s.capabilities=0;assert(MissionSequences(s).empty() && SequenceMemories(s,1).empty());
    std::cout<<"DNA navigation: native identities, separate side activities, hidden/locked rows and visible-only counts passed\n";
}

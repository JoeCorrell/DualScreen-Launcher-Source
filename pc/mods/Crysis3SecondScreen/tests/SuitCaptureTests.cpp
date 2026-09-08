#include "../src/SuitCapture.h"
#include <cassert>
#include <iostream>

int main() {
    c3::SuitCapture capture;
    auto add=[&](uint64_t generation,c3::Key key) {
        return capture.AddPerk(generation,key,"fixture-group","TEST ONLY",true,true,true,"fixture-class",false,"test description",2);
    };
    assert(!add(1,1)); assert(!capture.Read(1,1000).known);
    capture.ClearPerks(1); assert(add(1,1)); assert(!capture.Read(1,1000).known);
    assert(capture.UpdatePerks(1,1000));
    auto state=capture.Read(1,1000); assert(state.known && state.complete && state.size()==1);
    assert(state.entries[0].installed.known && state.entries[0].installed.value);
    assert(state.entries[0].unlocked.known && state.entries[0].unlocked.value);
    assert(!state.entries[0].progress.known && !state.entries[0].purchase.verified && !state.entries[0].toggle.verified);
    assert(!capture.Read(2,1000).known); assert(!capture.Read(1,999).known);
    assert(capture.Read(1,1750).known); assert(!capture.Read(1,1751).known);
    capture.ClearPerks(1); assert(!capture.Read(1,1000).known);
    assert(add(1,1)); assert(!add(1,1)); assert(!capture.UpdatePerks(1,1001));
    capture.ClearPerks(1); assert(!add(2,1)); assert(!capture.UpdatePerks(1,1001));
    capture.ClearPerks(2); for (int i=0;i<16;++i) assert(add(2,i+1));
    assert(!add(2,17)); assert(!capture.UpdatePerks(2,1001));
    capture.ClearPerks(2); assert(capture.UpdatePerks(2,1002));
    assert(capture.Read(2,1002).known && capture.Read(2,1002).size()==0);
    capture.Invalidate(3); assert(!capture.Read(2,1002).known);
    capture.ClearPerks(3);
    assert(!capture.AddPerk(3,1,"","",false,false,false,"",false,std::string(192,'x'),1));
    assert(!capture.UpdatePerks(3,1003));
    std::cout<<"PASS: suit capture transactions, duplicate/overflow rejection, stale/epoch rejection, no inferred actions or progress\n";
}

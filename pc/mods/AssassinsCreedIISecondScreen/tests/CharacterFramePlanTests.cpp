#include "CharacterFramePlan.h"
#include <cassert>
#include <cstdio>
using namespace ac2ds;
int main(){
    CharacterFramePlan plan;CharacterFrameTag tag{1,10,4};
    assert(plan.Begin(tag,{100,101,200}));assert(plan.Accept(tag,100,true));
    // Previous frame, shadow pass, previous world and another NPC cannot fill missing sections.
    for(auto other:{CharacterFrameTag{1,9,4},{1,10,5},{2,10,4}})assert(!plan.Accept(other,101,true));
    assert(!plan.Accept(tag,999,true));assert(!plan.Complete(tag));
    assert(plan.Accept(tag,101,true));assert(plan.Accept(tag,200,true));assert(plan.Complete(tag));
    assert(!plan.Accept(tag,100,true));assert(!plan.Complete(tag)); // Duplicate poisons assembly.
    assert(plan.Begin({1,11,4},{100,101,200}));assert(!plan.Complete({1,11,4}));
    assert(plan.Accept({1,11,4},100,true));assert(!plan.Accept({1,11,4},101,false));assert(!plan.Complete({1,11,4}));
    for(auto keys:{std::vector<uint64_t>{},std::vector<uint64_t>{1,1},std::vector<uint64_t>{0,1},std::vector<uint64_t>(257,1)})assert(!plan.Begin(tag,keys));
    assert(!plan.Begin({0,1,1},{1}));assert(!plan.Begin({1,0,1},{1}));assert(!plan.Begin({1,1,0},{1}));
    assert(plan.Begin(tag,{100}));assert(plan.Accept(tag,100,true));plan.Reset();assert(!plan.Complete(tag));
    std::puts("Character assembly: same frame/world/pass, all material sections, duplicate/failure retirement and bounded plans passed");
}

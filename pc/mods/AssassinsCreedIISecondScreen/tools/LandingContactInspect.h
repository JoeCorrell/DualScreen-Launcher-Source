#pragma once
// Diagnostic-only, exact AC II build. No engine calls or memory writes.
// 018fe9b0 rejects contacts whose cached component owner contains a
// BipedComponent or QuadrupedComponent. Descriptor ranges are assigned at run
// time; static descriptor addresses alone cannot establish membership.
#include "NativeReadTransaction.h"
#include <array>
#include <optional>

namespace ac2ds::landing_probe {
using namespace native;
enum class Filter { Unknown, Accept, Biped, Quadruped };
struct Range { uint16_t first{}, last{}; };
inline bool ReadRange(const Memory& m, uint32_t descriptor, Range& r) {
    return m.Member(descriptor,0x54,r.first) && m.Member(descriptor,0x56,r.last) &&
        r.first<=r.last && r.first!=0x7fff && r.last!=0x7fff;
}
inline bool ComponentRange(const Memory& m,uint32_t component,Range& range) {
    uint32_t table{},getter{},descriptor{},operand{};
    std::array<uint8_t,6> code{};
    if(!m.Read(component,table) || !m.Field(table,0x10,getter) ||
       getter<0x401000 || getter>=0x1ca0000 || !m.Copy(getter,code.data(),code.size()) ||
       code[5]!=0xc3) return false;
    std::memcpy(&operand,code.data()+1,4);
    // Accept only a verified trivial getter, never invoke a vtable function.
    if(code[0]==0xa1) { if(!m.Read(operand,descriptor))return false; }
    else if(code[0]==0xb8) descriptor=operand;
    else return false;
    return ReadRange(m,descriptor,range);
}
inline std::optional<bool> Contains(const Memory& m,uint32_t entity,const Range& wanted) {
    uint32_t array{};uint16_t packed{};
    if(!m.Field(entity,0x74,array) || !m.Member(entity,0x7a,packed))return {};
    const unsigned count=packed&0x3fff;
    if(count>256 || (count && array<0x10000))return {};
    // Native 0054e500/0054d0f0 lower_bound, then the native containment test.
    unsigned first=0,length=count;
    while(length){
        unsigned half=length/2,index=first+half;uint32_t component{};Range actual{};
        if(!m.Field(array,index*4,component) || !ComponentRange(m,component,actual))return {};
        if(actual.first<wanted.first){first=index+1;length-=half+1;}else length=half;
    }
    if(first==count)return false;
    uint32_t component{};Range actual{};
    if(!m.Field(array,first*4,component) || !ComponentRange(m,component,actual))return {};
    return actual.first>=wanted.first && actual.last<=wanted.last;
}
inline Filter InspectUnvalidated(const Memory& m,uint32_t contact) {
    uint32_t handle{},resolved{},generation{},expected{},component{},owner{};
    if(!m.Field(contact,0x34,handle) || !m.Reference(handle,resolved))return Filter::Unknown;
    // The native filter clears its cache on a dead reference. Reproduce only
    // the resulting decision, leaving the cache and reference counts untouched.
    if(!resolved)return Filter::Accept;
    if(!m.Field(resolved,0xe4,generation) || !m.Field(contact,0x30,expected))return Filter::Unknown;
    if(generation!=expected)return Filter::Accept;
    if(!m.Field(contact,0x2c,component))return Filter::Unknown;
    if(!component)return Filter::Accept;
    if(!m.Field(component,8,owner))return Filter::Unknown;
    if(!owner)return Filter::Accept;
    uint32_t descriptor{};Range wanted{};
    if(!m.Read(0x20c91a8,descriptor) || !ReadRange(m,descriptor,wanted))return Filter::Unknown;
    auto biped=Contains(m,owner,wanted);
    if(!biped)return Filter::Unknown;
    if(*biped)return Filter::Biped;
    if(!m.Read(0x209bc30,descriptor) || !ReadRange(m,descriptor,wanted))return Filter::Unknown;
    auto quadruped=Contains(m,owner,wanted);
    if(!quadruped)return Filter::Unknown;
    return *quadruped?Filter::Quadruped:Filter::Accept;
}
inline Filter Inspect(const Memory& source,uint32_t contact) {
    ReadTransaction tx(source);
    auto result=InspectUnvalidated(tx,contact);
    return tx.Validate()?result:Filter::Unknown;
}
inline const char* Name(Filter f) {
    switch(f){case Filter::Accept:return "accepted-by-component-filter";
    case Filter::Biped:return "excluded-biped";
    case Filter::Quadruped:return "excluded-quadruped";
    default:return "unknown-or-changing";}
}
}

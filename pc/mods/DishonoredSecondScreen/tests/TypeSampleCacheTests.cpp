#include "../src/GameMemory.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
using namespace dishonored;
struct Fixture {
    std::map<Address,uint8_t> bytes;
    unsigned calls{};
    template<class T> void put(Address at,const T& value) {
        const auto* p=reinterpret_cast<const uint8_t*>(&value);
        for(size_t i=0;i<sizeof(value);++i)bytes[at+Address(i)]=p[i];
    }
    void object(Address at,int index,Address type,int name) {
        std::array<uint32_t,14> header{};header[1]=uint32_t(index);header[10]=uint32_t(name);header[12]=type;put(at,header);
    }
    void name(unsigned index,const char* text) {
        Address at=0x40000+index*0x100;put(0x31000+index*4,at);put(at+8,uint8_t(0));
        for(unsigned i=0;i<=std::strlen(text);++i)put(at+0x10+i,uint8_t(text[i]));
    }
};
bool read(Address at,void* output,size_t count,void* context) noexcept {
    auto& f=*static_cast<Fixture*>(context);++f.calls;auto* p=static_cast<uint8_t*>(output);
    for(size_t i=0;i<count;++i) {auto found=f.bytes.find(at+Address(i));if(found==f.bytes.end())return false;p[i]=found->second;}
    return true;
}
int main() {
    Fixture f;constexpr Address image=0x400000,instance=0x20000,child=0x21000,parent=0x22000,meta=0x23000;
    f.put(image+layout::Objects,GameArray{0x30000,4,4});
    for(unsigned i=0;i<4;++i)f.put(0x30000+i*4,instance+i*0x1000);
    f.put(image+layout::Names,Address(0x31000));f.put(image+layout::NameCount,int32_t(3));
    f.object(instance,0,child,0);f.object(child,1,meta,0);f.object(parent,2,meta,1);f.object(meta,3,meta,2);
    f.put(child+layout::StructSuper,parent);f.put(parent+layout::StructSuper,Address(0));
    f.name(0,"Child");f.name(1,"Parent");f.name(2,"Class");
    Memory memory(image,read,&f);memory.Begin(65536,2048,true);
    assert(memory.IsA(instance,u"Parent"));unsigned first=f.calls;
    for(unsigned i=0;i<100;++i)assert(memory.IsA(instance,u"Parent"));
    assert(memory.TypeCacheHits()==100 && f.calls<=first+2);
    assert(!memory.IsA(instance,u"Unrelated"));assert(!memory.IsA(instance,u"Unrelated"));assert(memory.TypeCacheHits()==101);
    // Neither an old positive answer nor cached instance membership survives
    // a new pass, including same-address destruction and recreation.
    f.put(0x30000,Address(0));memory.Begin(65536,2048,true);
    assert(!memory.IsA(instance,u"Parent") && memory.TypeCacheHits()==0);
    f.put(0x30000,instance);f.put(child+layout::StructSuper,Address(0));memory.Begin(65536,2048,true);
    assert(!memory.IsA(instance,u"Parent"));
    f.put(child+layout::StructSuper,parent);memory.Begin(65536,2048);
    assert(memory.IsA(instance,u"Parent"));f.put(child+layout::StructSuper,Address(0));
    assert(!memory.IsA(instance,u"Parent") && memory.TypeCacheHits()==0);
    // A failed ancestry read must not become a cached negative result.
    memory.Begin(65536,2048,true);f.bytes.erase(child+layout::StructSuper);
    assert(!memory.IsA(instance,u"Parent"));f.put(child+layout::StructSuper,parent);
    assert(memory.IsA(instance,u"Parent") && memory.TypeCacheHits()==0);
    // A large reflected class must make progress across bounded samples.
    // With the old 512-entry cache this walk continually evicted its own
    // prefix, exhausted the budget and prevented later domains from running.
    Fixture large;large.put(image+layout::Names,Address(0x31000));large.put(image+layout::NameCount,int32_t(1200));
    for(unsigned i=0;i<1200;++i)large.name(i,"A_Long_Reflected_Field_Name_For_Budget_Recovery");
    Memory names(image,read,&large);unsigned passes{};bool complete=false;
    for(;passes<12 && !complete;++passes) {
        names.Begin(512*1024,8192,true);complete=true;
        for(int i=0;i<1200;++i) {char16_t output[96]{};uint32_t length{};
            if(!names.Name({i,0},output,96,length)) {complete=false;break;}
            assert(length==std::strlen("A_Long_Reflected_Field_Name_For_Budget_Recovery"));
        }
    }
    assert(complete);names.Begin(512*1024,8192,true);
    for(int i=0;i<1200;++i) {char16_t output[96]{};uint32_t length{};assert(names.Name({i,0},output,96,length));}
    assert(names.CallsUsed()==0);
    names.ClearNames();names.Begin(512*1024,8192,true);
    char16_t output[96]{};uint32_t length{};assert(names.Name({0,0},output,96,length));assert(names.CallsUsed()>0);
    puts("PASS: bounded ancestry reuse, negative matches, registry removal, recreation, uncached commands and failed-read recovery");
    std::printf("PASS: 1200-name reflection working set completes in %u bounded passes; warm pass uses zero name reads; lifecycle clears cache\n",passes);
}

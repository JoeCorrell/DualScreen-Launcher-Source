#include "NativeMapFixture.h"
#include "NativeMapMarkers.h"
#include <set>
struct MarkersFixture : Fixture {
    MarkersFixture(){
        Word(0x223f820,0);Word(0x1306c,0);Word(0x19024,0);
        Word(0x1b010,0x24000);Word(0x1b014,0x20002);Word(0x24000,0x30000);Word(0x24004,0x31000);
        Word(0x1900c,0);Word(0x19010,0);
        for(uint32_t p:{0x30000u,0x31000u}){
            Zero(p,0xd0);Word(p,0x1e765c0);Word(p+0x30,0x32000);bytes[p+0x3b]=1;
            Float(p+0x10,64);Float(p+0x14,128);
        }
        Ref(0x32000,0);Word(0x30020,12);bytes[0x3003a]=4;Word(0x31020,23);
        // ReadTransaction observes committed pages, with only used bytes checked.
        std::set<uint32_t> pages;for(const auto& byte:bytes)pages.insert(byte.first&~4095u);
        for(auto page:pages)for(uint32_t i=0;i<4096;++i)bytes.emplace(page+i,0);
    }
};
int main(){
    MarkersFixture f;auto map=ReadMap(f,f.roots);assert(map);f.calls=0;
    auto markers=ReadMapMarkers(f,f.roots,*map);assert(markers && markers->size()==1);
    assert((*markers)[0].type==12 && (*markers)[0].x==.625f && (*markers)[0].y==.25f);
    auto calls=f.calls;for(size_t i=1;i<=calls;++i){MarkersFixture broken;broken.calls=0;broken.fail=i;assert(!ReadMapMarkers(broken,broken.roots,*map));}
    f.bytes[0x3103a]=1;markers=ReadMapMarkers(f,f.roots,*map);assert(markers && markers->size()==2);
    f.bytes[0x3103a]=3;markers=ReadMapMarkers(f,f.roots,*map);assert(markers && markers->size()==1);
    f.bytes[0x3003b]=0;markers=ReadMapMarkers(f,f.roots,*map);assert(markers && markers->empty());
    f.bytes[0x3003b]=1;f.Word(0x30000,0x1e765c1);markers=ReadMapMarkers(f,f.roots,*map);assert(markers && markers->empty());
    MarkerContext context;context.notoriety=false;context.region=false;
    MarkersFixture rules;rules.Word(0x30020,31);assert(MarkerVisible(rules,0x30000,context)==true);
    context.notoriety=true;assert(MarkerVisible(rules,0x30000,context)==false);
    rules.Word(0x30020,4);assert(MarkerVisible(rules,0x30000,context)==false);
    context.region=true;assert(MarkerVisible(rules,0x30000,context)==true);
    rules.bytes[0x3003a]=0;assert(MarkerVisible(rules,0x30000,context)==false);
    context.flags=2;assert(MarkerVisible(rules,0x30000,context)==true);context.flags=0;
    rules.Word(0x30000,0x1e9a510);rules.Word(0x300b0,2);rules.bytes[0x3003a]=1;
    assert(MarkerVisible(rules,0x30000,context)==true);rules.Word(0x300b0,1);assert(MarkerVisible(rules,0x30000,context)==false);
    rules.Word(0x30000,0x1e7f7f0);rules.Word(0x30028,0x24000);rules.Word(0x3002c,0x20002);
    rules.Word(0x300b0,0x32000);rules.Word(0x300b4,0x20002);rules.bytes[0x32000]=1;rules.bytes[0x32001]=0;
    assert(MarkerVisible(rules,0x30000,context)==true);rules.bytes[0x32001]=1;assert(MarkerVisible(rules,0x30000,context)==false);
    MarkersFixture transaction;ReadTransaction read(transaction);uint32_t value{};
    assert(read.Read(0x30020,value) && value==12);transaction.bytes[0x30024]=42;assert(read.Validate());
    transaction.Word(0x30020,13);assert(!read.Validate()); // Relevant field changed, unrelated neighbor ignored.
    MarkersFixture retained;retained.Ref(0x32000,0x30000);ReadTransaction reference(retained);
    assert(reference.Reference(0x32000,value) && value==0x30000);
    retained.Word(0x32004,20);assert(reference.Validate()); // Retaining the resource does not change identity/state.
    retained.Word(0x32000,0x31000);assert(!reference.Validate());
    MarkersFixture unloaded;unloaded.Ref(0x32000,0x30000);ReadTransaction residency(unloaded);
    assert(residency.Reference(0x32000,value));unloaded.Word(0x32008,0);assert(!residency.Validate());
    std::puts("Native marker discovery, native rule dispatch, group state and page transaction tests passed");
}

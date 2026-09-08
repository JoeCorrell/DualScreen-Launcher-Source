#include "NativeMapFixture.h"
int main(){
    Fixture f;auto map=ReadMap(f,f.roots);assert(map && map->artwork==0x52a019fa && map->title==123);
    assert(map->x==0.625f && map->y==0.25f && map->discovery[12]==255 && map->discovery[3]==127);
    assert(!map->heading); // Degenerate/vertical forward projection keeps position-only marker.
    auto calls=f.calls;
    for(size_t i=1;i<=calls;++i){Fixture broken;broken.fail=i;assert(!ReadMap(broken,broken.roots));}
    for(uint32_t a:{0x23000u,0x1e054u,0x1d000u,0x1c00cu,0x13040u,0x13020u,0x13024u,0x1b034u,0x19004u,0x1a000u,0x12060u,0x18000u,0x223f814u}){
        Fixture changed;changed.mutate=a;assert(!ReadMap(changed,changed.roots));
    }
    for(auto invalid:std::vector<std::pair<uint32_t,uint32_t>>{{0x13050,1},{0x1e054,0},{0x1e014,5},{0x1e008,513},
        {0x1e00c,0},{0x1e010,2},{0x1b034,0},{0x1b03c,0},{0x1b048,0x7fc00000},{0x19004,0x77777},{0x1d008,0}}){
        Fixture bad;bad.Word(invalid.first,invalid.second);assert(!ReadMap(bad,bad.roots));
    }
    Fixture moving;moving.Float(0x13040,1000000);assert(!ReadMap(moving,moving.roots));
    // Exact combat controller source-kind 0 returns the same player's cached
    // world matrix, not a relative parent transform. Dynamic/foreign owners fail.
    Fixture controlled;controlled.Word(0x13050,0x30000);controlled.Zero(0x30040,24);
    controlled.Word(0x30040,controlled.roots.player);
    map=ReadMap(controlled,controlled.roots);assert(map && map->x==.625f && map->y==.25f);
    auto controlledCalls=controlled.calls;
    for(size_t i=1;i<=controlledCalls;++i){
        Fixture interrupted;interrupted.Word(0x13050,0x30000);interrupted.Zero(0x30040,24);
        interrupted.Word(0x30040,interrupted.roots.player);interrupted.fail=i;
        assert(!ReadMap(interrupted,interrupted.roots));
    }
    for(uint32_t kind:{1u,2u,3u,UINT32_MAX}){
        controlled.Word(0x30050,kind);assert(!ReadMap(controlled,controlled.roots));
    }
    controlled.Word(0x30050,0);controlled.Word(0x30040,0x33000);assert(!ReadMap(controlled,controlled.roots));
    auto providerFixture=[](){Fixture f;f.Word(0x13050,0x30000);f.Zero(0x30040,24);
        f.Word(0x30040,f.roots.player);f.Word(0x30050,1);f.Word(0x30054,0x31000);f.Word(0x31000,0);return f;};
    auto provider=providerFixture();map=ReadMap(provider,provider.roots);
    assert(map && map->x==.625f && map->y==.25f);
    for(size_t i=1;i<=provider.calls;++i){auto bad=providerFixture();bad.fail=i;assert(!ReadMap(bad,bad.roots));}
    for(uint32_t address:{0x30040u,0x30050u,0x30054u,0x31000u}){
        auto changed=providerFixture();changed.mutate=address;assert(!ReadMap(changed,changed.roots));
    }
    for(uint32_t flags:{1u,0x80000000u,UINT32_MAX}){
        auto dirty=providerFixture();dirty.Word(0x31000,flags);assert(!ReadMap(dirty,dirty.roots));
    }
    Fixture unfogged;unfogged.Ref(0x1d000,0,0);assert(!ReadMap(unfogged,unfogged.roots)); // Loading city must not reveal fog.
    unfogged.Word(0x1c00c,0xd8508c6e);map=ReadMap(unfogged,unfogged.roots);assert(map && map->discovery==std::vector<uint8_t>{255});
    Fixture unloaded;unloaded.Ref(0x1d000,0,456);assert(!ReadMap(unloaded,unloaded.roots));
    Fixture bypass;bypass.bytes[0x19024]=2;bypass.Word(0x1e054,0);map=ReadMap(bypass,bypass.roots);assert(map && map->discovery.size()==1);
    Fixture wide;wide.Word(0x1e008,512);wide.Word(0x1e00c,256);wide.Zero(0x23000,512*256);wide.bytes[0x23000]=255;
    map=ReadMap(wide,wide.roots);assert(map && map->discovery.size()==512*256 && map->discovery[512*255]==255);
    Fixture facing;facing.Float(0x13024,1);map=ReadMap(facing,facing.roots);assert(map && map->heading && std::abs(*map->heading)<1e-5);
    facing.Float(0x13020,1);facing.Float(0x13024,0);map=ReadMap(facing,facing.roots);assert(map && map->heading && std::abs(*map->heading-1.57079633f)<1e-5);
    facing.Float(0x13020,0);facing.Float(0x13024,-1);map=ReadMap(facing,facing.roots);assert(map && map->heading && std::abs(std::abs(*map->heading)-3.14159265f)<1e-5);
    facing.Float(0x13020,1);facing.Float(0x13024,1);facing.Float(0x1b03c,4);map=ReadMap(facing,facing.roots);
    assert(map && map->heading && std::abs(*map->heading-std::atan2(4.f,2.f))<1e-5);
    facing.Word(0x13020,0x7fc00000);map=ReadMap(facing,facing.roots);assert(map && !map->heading);
    std::puts("Native map projection, discovery origin, unreadable state and transition guards passed");
}

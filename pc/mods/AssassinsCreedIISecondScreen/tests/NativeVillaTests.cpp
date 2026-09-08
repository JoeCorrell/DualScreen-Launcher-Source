#include "NativeSynchronizationFixture.h"
#include "NativeVilla.h"
struct VillaFixture : Fixture {
    VillaFixture(){
        Word(0x20000,0x1e610c8);Word(0x30010,4);Word(0x30008,0x31000);Word(0x3000c,0x10001);
        Word(0x40000,0x1e61020);Word(0x40014,18);Word(0x40008,0x72000);
        Word(0x80000,0x1e699bc);Word(0x80030,0);
        Word(roots.progress,0x1e5dbd0);Word(roots.progress+0xc,1280);Word(roots.progress+0x10,150000);
    }
};
int main(){
    VillaFixture f;auto v=ReadVilla(f,f.roots);assert(v && v->unlocked);
    assert(v->restored==1 && v->value==1280 && v->population==29);
    auto calls=f.calls;for(size_t i=1;i<=calls;++i){VillaFixture broken;broken.fail=i;assert(!ReadVilla(broken,broken.roots));}
    for(uint32_t a:{0x223f814u,0x223f81cu,0x20000u,0x30010u,0x40008u,0x40014u,0x80030u}){
        VillaFixture changed;changed.mutate=a;assert(!ReadVilla(changed,changed.roots));
    }
    VillaFixture locked;locked.Word(0x80030,1);v=ReadVilla(locked,locked.roots);assert(v && !v->unlocked && !v->value && !v->restored);
    VillaFixture unresolved;unresolved.Word(0x72008,0);v=ReadVilla(unresolved,unresolved.roots);assert(v && !v->unlocked);
    VillaFixture absent;absent.Word(0x40014,19);v=ReadVilla(absent,absent.roots);assert(v && !v->unlocked);
    VillaFixture complete;complete.Word(complete.roots.progress+0xc,150000);v=ReadVilla(complete,complete.roots);assert(v && v->restored==100 && v->population==3500);
    VillaFixture zero;zero.Word(zero.roots.progress+0xc,0);v=ReadVilla(zero,zero.roots);assert(v && v->restored==0 && v->population==0);
    VillaFixture invalid;invalid.Word(invalid.roots.progress+0x10,0);assert(!ReadVilla(invalid,invalid.roots));
    invalid.Word(invalid.roots.progress+0x10,150000);invalid.Word(invalid.roots.progress+0xc,UINT32_MAX);assert(!ReadVilla(invalid,invalid.roots));
    std::puts("Villa native unlock, restoration, value, population and read-failure tests passed");
}

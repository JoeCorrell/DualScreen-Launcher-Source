#include "NativeCapeFixture.h"
int main(){
    CapeFixture f;auto capes=ReadCapes(f.m,0x40000);assert(capes && capes->size()==2);
    assert((*capes)[0].id==777 && (*capes)[0].equipped && (*capes)[1].id==778 && !(*capes)[1].equipped);
    auto calls=f.m.reads;for(unsigned i=1;i<=calls;++i){CapeFixture broken;broken.m.failAt=i;assert(!ReadCapes(broken.m,0x40000));}
    CapeFixture locked;locked.m.Word(0x52070,1);capes=ReadCapes(locked.m,0x40000);assert(capes && capes->size()==1);
    CapeFixture priority;priority.m.Array(0x12118,0x13000,2);capes=ReadCapes(priority.m,0x40000);
    assert(capes && !(*capes)[0].equipped && (*capes)[1].equipped);
    CapeFixture duplicate;duplicate.m.Array(0x12118,0x13000,2);duplicate.m.Word(0x50024,0x51000);
    capes=ReadCapes(duplicate.m,0x40000);assert(capes && !(*capes)[0].equipped && !(*capes)[1].equipped);
    CapeFixture notWorn;notWorn.m.Array(0x12118,0x13000,0);capes=ReadCapes(notWorn.m,0x40000);
    assert(capes && capes->size()==2 && !(*capes)[0].equipped && !(*capes)[1].equipped);
    CapeFixture costume;costume.m.Word(0x68054,39);capes=ReadCapes(costume.m,0x40000);assert(capes && capes->size()==1);
    for(uint32_t a:{0x62000u,0x52000u,0x30000u,0x12000u,0x11000u}){
        CapeFixture changed;changed.m.mutateAddress=a;assert(!ReadCapes(changed.m,0x40000));
    }
    std::puts("Native cape unlock, equipped subtype, priority and transition tests passed");
}

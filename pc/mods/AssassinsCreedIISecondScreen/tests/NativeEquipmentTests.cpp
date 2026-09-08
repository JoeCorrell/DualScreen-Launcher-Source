#include "NativeEquipmentFixture.h"
int main(){
    EquipmentFixture f;auto result=ac2ds::native::ReadEquipment(f.m,0x40000);
    assert(result && result->size()==1 && result->front().id==777 && result->front().equipped);
    assert(result->front().artworkId==888 && result->front().title==101);
    const auto reads=f.m.reads;
    for(unsigned i=1;i<=reads;++i){EquipmentFixture interrupted;interrupted.m.failAt=i;assert(!ReadEquipment(interrupted.m,0x40000));}
    EquipmentFixture locked;locked.m.Word(0x52030,1);result=ReadEquipment(locked.m,0x40000);assert(result && result->empty());
    EquipmentFixture unowned;unowned.m.Array(0x12118,0x13000,0);result=ReadEquipment(unowned.m,0x40000);assert(result && result->empty());
    EquipmentFixture mismatch;mismatch.m.Word(0x5200c,3);assert(!ReadEquipment(mismatch.m,0x40000));
    for(uint32_t a:{0x400d5u,0x42008u,0x12118u,0x13000u,0x50004u,0x51000u,0x52030u,0x68000u,0x63000u,0x65010u,0x69000u}){
        EquipmentFixture changed;changed.m.mutateAddress=a;assert(!ReadEquipment(changed.m,0x40000));
    }
    std::puts("Carried equipment ownership, catalog locks, artwork IDs and transition tests passed");
}

#include "NativeSynchronizationFixture.h"
#include "NativeCollectibles.h"
struct CollectionsFixture : Fixture {
    void Array(uint32_t a,uint32_t p,uint32_t n){Word(a,p);Word(a+4,n|(n<<16));}
    CollectionsFixture(){
        for(uint32_t offset:{0x24u,0x2cu})Word(0x20000+offset,0xa0000);
        Ref(0xa0000,0xa1000);Word(0xa1000,0x1e9efd0);Array(0xa1018,0xa2000,1);
        Word(0xa2000,0xa3000);Ref(0xa3000,0xa4000);Word(0xa4000,0x1e9ef50);Array(0xa403c,0xa5000,1);
        Word(0xa5000,0xa6000);Ref(0xa6000,0xa7000);Word(0xa7000,0x1e692d8);Array(0xa7018,0xa8000,3);
        for(uint32_t i=0;i<3;++i){Word(0xa8000+i*4,0xa9000+i*16);Ref(0xa9000+i*16,0x80000+i*128);
            Word(0x80000+i*128,0x1ccf940);Word(0x8001c+i*128,i==0?3:i==1?5:2);}
    }
    void Treasure(){
        Word(0xa7000,0x1e99c78);Word(0xa7018,0xb0000);Ref(0xb0000,0xb1000);
        Word(0xb1000,0x1e71ef8);Array(0xb10e0,0xa8000,3);
        for(uint32_t i=0;i<3;++i){Word(0x80000+i*128,0x1e699bc);Word(0x8000c+i*128,9);}
        Word(0x223c590,0xb2000);Word(0xb2000,0x1e3c920);Array(0xb2030,0xb3000,2);
        Word(0xb3000,0xa9000);Word(0xb3004,0xa9020);
    }
};
int main(){
    for(auto kind:{CollectibleKind::Viewpoints,CollectibleKind::Treasure}){
        CollectionsFixture f;if(kind==CollectibleKind::Treasure)f.Treasure();
        auto value=ReadAdditionalCollection(f,f.roots,kind);assert(value && value->completed==2 && value->total==3);
        auto calls=f.calls;
        for(size_t i=1;i<=calls;++i){CollectionsFixture broken;if(kind==CollectibleKind::Treasure)broken.Treasure();
            broken.fail=i;assert(!ReadAdditionalCollection(broken,broken.roots,kind));}
        for(uint32_t address:{0x16000u,0xa1000u,0xa4000u,0xa7000u,0x80000u}){
            CollectionsFixture changed;if(kind==CollectibleKind::Treasure)changed.Treasure();changed.mutate=address;
            assert(!ReadAdditionalCollection(changed,changed.roots,kind));
        }
        CollectionsFixture unloaded;if(kind==CollectibleKind::Treasure)unloaded.Treasure();unloaded.Word(0xa9008,0);
        value=ReadAdditionalCollection(unloaded,unloaded.roots,kind);assert(value && value->completed==1 && value->total==3);
    }
    for(auto change:{std::pair<uint32_t,uint32_t>{0xa101c,0x410041},{0xa4040,0x410041},
            {0xa701c,0x10011001},{0xa1000,0},{0xa4000,0},{0xa7000,0},{0x80000,0},{0x8001c,6}}){
        CollectionsFixture f;f.Word(change.first,change.second);assert(!ReadAdditionalCollection(f,f.roots,CollectibleKind::Viewpoints));
    }
    CollectionsFixture duplicate;duplicate.Array(0xa403c,0xa5000,2);duplicate.Word(0xa5004,0xa6000);
    assert(!ReadAdditionalCollection(duplicate,duplicate.roots,CollectibleKind::Viewpoints));
    for(auto change:{std::pair<uint32_t,uint32_t>{0xb10e4,0x1000100},{0xb2000,0},{0xb1000,0},{0x8000c,2}}){
        CollectionsFixture f;f.Treasure();f.Word(change.first,change.second);
        assert(!ReadAdditionalCollection(f,f.roots,CollectibleKind::Treasure));
    }
    CollectionsFixture identity;identity.Treasure();identity.Ref(0xb4000,0x80000);identity.Word(0xb3000,0xb4000);
    auto value=ReadAdditionalCollection(identity,identity.roots,CollectibleKind::Treasure);
    assert(value && value->completed==1); // Equal object through a different handle is not native ownership.
    CollectionsFixture empty;empty.Treasure();empty.Word(0xb0008,0);
    value=ReadAdditionalCollection(empty,empty.roots,CollectibleKind::Treasure);assert(value && !value->total && !value->completed);
    std::puts("Native viewpoint/treasure totals: native state and handle semantics, bounds, unloads and transaction guards passed");
}

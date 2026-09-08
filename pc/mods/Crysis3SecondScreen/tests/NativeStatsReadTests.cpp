#include "../src/NativeStatsRead.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

struct Memory {
    std::map<uint32_t,uint8_t> data;
    bool operator()(uint32_t address,void* output,size_t size){
        for(size_t i=0;i<size;++i){auto it=data.find(address+uint32_t(i));if(it==data.end())return false;static_cast<uint8_t*>(output)[i]=it->second;}
        return true;
    }
    void Word(uint32_t address,uint32_t value){for(int i=0;i<4;++i)data[address+i]=uint8_t(value>>(i*8));}
};
int main(int argc,char** argv){
    assert(argc==2);
    constexpr uint32_t root=0x10000,actor=0x20000,stats=0x30000,entity=17;
    Memory memory;memory.Word(c3native::actor_detail::RootSlot,root);memory.Word(root,c3native::actor_detail::RootVtable);
    memory.Word(root+0xd4,stats);memory.Word(0x3ba0ba50,stats);memory.Word(stats,0x39a1a6b4);
    memory.Word(actor,c3native::actor_detail::ActorVtable);memory.Word(actor+8,entity);
    for(uint32_t i=0;i<283;++i)memory.Word(stats+0x22c+i*4,0);
    memory.Word(stats+0x22c+62*4,13);
    auto guard=[] {return c3native::ActorFrameGuard{7,41,41,0,true,true};};
    auto out=std::make_unique<c3::List<c3::Statistic,48>>();
    auto run=[&](auto& m){return c3native::ReadStatistics(m,root,actor,entity,true,guard(),guard,*out);};
    using F=c3native::StatsReadFailure;
    assert(run(memory)==F::None&&out->known&&out->count==17);
    assert(out->entries[4].name.str()=="HEADSHOT KILLS"&&out->entries[4].value.value==13);
    auto bad=memory;bad.Word(0x3ba0ba50,stats+4);assert(run(bad)==F::OwnerMismatch&&!out->known);
    bad=memory;bad.Word(actor+8,entity+1);assert(run(bad)==F::OwnerMismatch);
    bad=memory;bad.Word(stats,0);assert(run(bad)==F::OwnerMismatch);
    bad=memory;bad.Word(stats+0x634+4,0x7fc00000);assert(run(bad)==F::InvalidValue&&!out->known);
    bad=memory;bad.Word(stats+0x22c+62*4,0xffffffff);assert(run(bad)==F::InvalidValue&&!out->known);
    assert(c3native::ReadStatistics(memory,root,actor,entity,false,guard(),guard,*out)==F::GuardUnverified);
    auto changed=[] {return c3native::ActorFrameGuard{8,41,41,0,true,true};};
    assert(c3native::ReadStatistics(memory,root,actor,entity,true,guard(),changed,*out)==F::ChangedDuringRead);
    int samples=0;auto changing=[&](uint32_t address,void* bytes,size_t size){
        const bool ok=memory(address,bytes,size);
        if(address==stats+0x22c&&++samples==2)static_cast<uint8_t*>(bytes)[0]^=1;
        return ok;
    };
    assert(run(changing)==F::ChangedDuringRead&&!out->known);
    const auto capture=std::filesystem::path(argv[1])/".work/crysis3/live-234100/read10/read-10.bin";
    if(std::filesystem::exists(capture)){
        std::ifstream input(capture,std::ios::binary);c3native::stats_detail::Raw raw;
        assert(input.read(reinterpret_cast<char*>(raw.words.data()),sizeof(raw.words)));
        assert(c3native::stats_detail::Decode(raw,*out)==F::None);
        assert(out->entries[4].value.value==13&&out->entries[9].value.value==7);
        assert(out->entries[10].value.value==26&&out->entries[12].value.value==275&&out->entries[13].value.value==111);
        assert(out->entries[14].value.value>6213&&out->entries[14].value.value<6214);
    }
    std::cout<<"PASS: statistics owner/epoch guards, complete-read stability, finite values, unknown profile rejection and live counter replay\n";
}

#include "../src/NativeObjectiveRead.h"
#include "../src/NativeObjectiveProfile.h"
#include "../src/NativeActorRead.h"
#include <cassert>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

namespace {
using Failure=c3native::ObjectiveReadFailure;
constexpr uint32_t Root=0x100000, Framework=0x110000, LevelSystem=0x120000, Level=0x130000;
constexpr uint32_t Objectives=0x140000, Records=0x150000, Strings=0x160000;
struct Memory {
    std::map<uint32_t,std::vector<unsigned char>> blocks;
    std::function<void(uint32_t,size_t)> beforeRead;
    size_t calls{}, bytes{};
    void Add(uint32_t address,size_t count) { blocks[address].resize(count); }
    void Word(uint32_t address,uint32_t value) {
        auto it=blocks.upper_bound(address); assert(it!=blocks.begin()); --it;
        assert(uint64_t(address)+4<=uint64_t(it->first)+it->second.size());
        std::memcpy(it->second.data()+address-it->first,&value,4);
    }
    void Byte(uint32_t address,unsigned char value) {
        auto it=blocks.upper_bound(address); assert(it!=blocks.begin()); --it;
        assert(uint64_t(address)<uint64_t(it->first)+it->second.size());
        it->second[address-it->first]=value;
    }
    bool Read(uint32_t address,void* out,size_t count) {
        ++calls; bytes+=count; assert(count && count<=4096);
        if (beforeRead) beforeRead(address,count);
        auto it=blocks.upper_bound(address); if (it==blocks.begin()) return false; --it;
        if (uint64_t(address)+count>uint64_t(it->first)+it->second.size()) return false;
        std::memcpy(out,it->second.data()+address-it->first,count); return true;
    }
};
struct Fixture {
    Memory memory;
    uint32_t stringCursor=Strings;
    c3native::ActorFrameGuard guard{1,7,7,0,true,true};
    std::unique_ptr<c3native::NativeObjectiveScratch> scratch=std::make_unique<c3native::NativeObjectiveScratch>();
    std::unique_ptr<c3native::NativeObjectiveValues> values=std::make_unique<c3native::NativeObjectiveValues>();
    uint32_t title{},description{},identity{};
    uint32_t String(const std::string& text) {
        const auto address=stringCursor; stringCursor+=256;
        assert(text.size()<256);
        for (size_t i=0;i<text.size();++i) memory.Byte(address+uint32_t(i),static_cast<unsigned char>(text[i]));
        return address;
    }
    void Record(size_t index,uint32_t state,const std::string& id,const std::string& name,const std::string& detail) {
        const uint32_t address=Records+uint32_t(index)*40;
        const auto nameAddress=String(name),detailAddress=String(detail),idAddress=String(id);
        memory.Word(address+4,nameAddress); memory.Word(address+8,detailAddress); memory.Word(address+12,idAddress);
        memory.Word(address+28,state);
        if (!index) { title=nameAddress; description=detailAddress; identity=idAddress; }
    }
    void Extent(uint32_t count) {
        memory.Word(Objectives+0x1c,Records); memory.Word(Objectives+0x20,Records+count*40);
        memory.Word(Objectives+0x24,Records+count*40);
    }
    Fixture() {
        memory.Add(0x3b9fde30,4); memory.Word(0x3b9fde30,Root);
        memory.Add(Root,0x108); memory.Word(Root,0x399bef80);
        memory.Word(Root+0x30,Framework); memory.Word(Root+0x104,Objectives);
        memory.Add(Framework,0x4b0); memory.Word(Framework,0x398ba5e0); memory.Word(Framework+0x4ac,LevelSystem);
        memory.Add(LevelSystem,32); memory.Word(LevelSystem,0x398868a0); memory.Word(LevelSystem+0x1c,Level);
        memory.Add(Level,16); memory.Word(Level,0x398862c4); memory.Word(Level+4,0x398862d0);
        memory.Add(Objectives,40); memory.Word(Objectives,0x399f32c0);
        memory.Add(Records,256*40); memory.Add(Strings,0x40000);
        for (uint32_t i=0;i<256;++i) memory.Word(Records+i*40,Objectives);
        memory.Word(Level+8,String("fields")); memory.Word(Level+12,String("levels/fields"));
        Extent(4);
        Record(0,3,"Fields.Fields_Objective_04","@fields_objective_04_name","@fields_objective_04_description");
        Record(1,1,"Fields.Fields_Objective_03","@fields_objective_03_name","@fields_objective_03_description");
        Record(2,2,"Fields.Fields_Objective_S02","@fields_objective_S02_name","@fields_objective_S02_description");
        memory.Byte(Records+2*40+0x25,1);
        // Inactive entries do not require reading their unused string pointers.
        memory.Word(Records+3*40+4,0xffffffff);
    }
    Failure Run(bool profile=true,bool phase=true) {
        auto read=[&](uint32_t address,void* out,size_t count) { return memory.Read(address,out,count); };
        return c3native::ReadCurrentObjectives(read,[&]{return guard;},profile,phase,Root,*scratch,*values);
    }
};
void Cleared(const c3native::NativeObjectiveValues& value) {
    assert(!value.levelName.bytes[0] && !value.levelPath.bytes[0]);
    assert(!value.missionTitle.bytes[0] && !value.location.bytes[0]);
    assert(!value.objectives.known && !value.objectives.count && !value.noninactiveCount && !value.nativeRecordCount);
}
int Replay(const char* path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if (!input || input.tellg()>2*1024*1024) return 2;
    input.seekg(0);
    uint32_t magic{},count{};
    if (!input.read(reinterpret_cast<char*>(&magic),4) || !input.read(reinterpret_cast<char*>(&count),4) ||
        magic!=0x524f3343 || !count || count>256) return 2;
    Memory memory;
    for (uint32_t i=0;i<count;++i) {
        uint32_t address{},size{};
        if (!input.read(reinterpret_cast<char*>(&address),4) || !input.read(reinterpret_cast<char*>(&size),4) ||
            address<0x10000 || !size || size>65536 || uint64_t(address)+size>0x100000000ull || memory.blocks.count(address)) return 2;
        memory.Add(address,size);
        if (!input.read(reinterpret_cast<char*>(memory.blocks[address].data()),size)) return 2;
    }
    if (input.peek()!=std::char_traits<char>::eof()) return 2;
    uint32_t root{};
    if (!memory.Read(0x3b9fde30,&root,4)) return 3;
    auto scratch=std::make_unique<c3native::NativeObjectiveScratch>();
    auto values=std::make_unique<c3native::NativeObjectiveValues>();
    const c3native::ActorFrameGuard replayGuard{1,1,1,0,true,true};
    auto read=[&](uint32_t address,void* out,size_t bytes) {
        if (memory.Read(address,out,bytes)) return true;
        std::cerr<<"Missing copied bytes at "<<std::hex<<address<<std::dec<<" length "<<bytes<<'\n'; return false;
    };
    // Synthetic scope/profile flags exercise the copied-byte reader ONLY.
    // Runtime profile verification and lifecycle attestation are not implied.
    const auto result=c3native::ReadCurrentObjectives(read,[&]{return replayGuard;},true,true,root,*scratch,*values);
    if (result!=Failure::None) { std::cerr<<"Replay failure "<<int(result)<<'\n'; return 4; }
    std::cout<<"COPIED-CAPTURE REPLAY ONLY: "<<values->levelName.str()<<" / "<<values->levelPath.str()<<'\n';
    std::cout<<values->missionTitle.str()<<" | "<<values->location.str()<<'\n';
    std::cout<<values->nativeRecordCount<<" records, "<<values->noninactiveCount<<" noninactive, "<<values->objectives.count<<" displayed\n";
    for (size_t i=0;i<values->objectives.size();++i) {
        const auto& objective=values->objectives.entries[i];
        std::cout<<int(objective.state)<<" | "<<objective.title.str()<<" | "<<objective.description.str()<<'\n';
    }
    std::cout<<"Copied reads: "<<memory.calls<<", bytes: "<<memory.bytes<<"; no atomic/lifetime claim\n";
    return 0;
}
}
int main(int argc,char** argv) {
    if (argc==2) return Replay(argv[1]);
    assert(argc==1);
    assert(c3::objective_catalog::Count==298 && c3native::NativeObjectiveProfileCount==15);
    assert(c3::objective_catalog::LevelCount==8);
    for (const auto& level:c3::objective_catalog::Levels) {
        c3::Text name{},title{},location{};
        std::memcpy(name.bytes.data(),level.level.data(),level.level.size());
        c3native::objective_detail::LevelEnglish(name,title,location);
        assert(title.str()==level.title && location.str()==level.location);
    }
    {
        c3::Text name{},title{},location{};
        std::memcpy(name.bytes.data(),"Fields",6);
        c3native::objective_detail::LevelEnglish(name,title,location);
        assert(title.str()=="WELCOME TO THE JUNGLE");
        std::memcpy(name.bytes.data(),"custom",6);
        c3native::objective_detail::LevelEnglish(name,title,location);
        assert(title.str()=="custom" && location.str().empty());
    }
    for (size_t i=0;i<c3::objective_catalog::Count;++i) {
        const auto& entry=c3::objective_catalog::Entries[i];
        assert(!entry.key.empty() && !entry.english.empty() && entry.english.size()<192);
        if (i) assert(c3::objective_catalog::Entries[i-1].key<entry.key);
        c3::Text key{}; std::memcpy(key.bytes.data(),entry.key.data(),entry.key.size());
        assert(c3native::objective_detail::English(key).str()==entry.english);
    }
    {
        Fixture f; assert(f.Run()==Failure::None);
        const auto& out=*f.values;
        assert(out.levelName.str()=="fields" && out.levelPath.str()=="levels/fields");
        assert(out.missionTitle.str()=="WELCOME TO THE JUNGLE");
        assert(out.location.str()=="WELCOME TO THE JUNGLE / West Side Yard, Penn Station, NY / 08:05, November 29th, 2047");
        assert(out.objectives.known && out.objectives.complete && out.objectives.count==3);
        assert(out.nativeRecordCount==4 && out.noninactiveCount==3);
        assert(out.objectives.entries[0].title.str()=="Reach the North Railyard");
        assert(out.objectives.entries[0].description.str()=="Reach the North Railyard Through the Subway");
        assert(out.objectives.entries[0].state==c3::ObjectiveState::Active && out.objectives.entries[0].key==1);
        assert(out.objectives.entries[1].state==c3::ObjectiveState::Failed && out.objectives.entries[1].optional.value);
        assert(out.objectives.entries[2].state==c3::ObjectiveState::Complete && !out.objectives.entries[2].optional.value);
        assert(f.memory.calls<500 && f.memory.bytes<20000);
        assert(f.Run(false)==Failure::GuardUnverified); Cleared(*f.values);
        assert(f.Run(true,false)==Failure::GuardUnverified); Cleared(*f.values);
        f.guard.currentThread=8; assert(f.Run()==Failure::GuardUnverified);
        f.guard.currentThread=7; f.guard.systemEventDepth=1; assert(f.Run()==Failure::GuardUnverified);
    }
    for (int test=0;test<10;++test) {
        Fixture f;
        switch(test) {
        case 0: f.memory.Word(Objectives+0x20,Records-4); break;
        case 1: f.memory.Word(Objectives+0x20,Records+41); break;
        case 2: f.memory.Word(Objectives+0x24,Records+40); break;
        case 3: f.memory.Word(Objectives+0x1c,Records+1); break;
        case 4: f.memory.Word(Objectives+0x20,Records+257*40); f.memory.Word(Objectives+0x24,Records+257*40); break;
        case 5: f.memory.Word(Objectives+0x1c,0); break;
        case 6: f.memory.Word(Objectives+0x1c,4); f.memory.Word(Objectives+0x20,4); f.memory.Word(Objectives+0x24,4); break;
        case 7: f.memory.Word(Objectives+0x1c,0xfffffff0); f.memory.Word(Objectives+0x20,0x30); f.memory.Word(Objectives+0x24,0x30); break;
        case 8: f.memory.Word(Objectives+0x24,Records+4*40+4); break;
        case 9: f.memory.Word(Objectives+0x24,Records+4097*40); break;
        }
        assert(f.Run()==Failure::InvalidVector); Cleared(*f.values);
    }
    for (const auto address:{Root,Framework,LevelSystem,Level,Level+4,Objectives}) {
        Fixture f; f.memory.Word(address,0xdeadbeef);
        assert(f.Run()==Failure::UnsupportedLayout); Cleared(*f.values);
    }
    for (const auto address:{Records+0x1c,Records+0x24,Records+0x25}) {
        Fixture f; f.memory.Word(address,255);
        assert(f.Run()==Failure::InvalidRecord); Cleared(*f.values);
    }
    for (const auto index:{0u,3u}) {
        Fixture f; f.memory.Word(Records+index*40,Objectives+4);
        assert(f.Run()==Failure::InvalidRecord); Cleared(*f.values); // active AND inactive back-references
    }
    {
        Fixture f; f.memory.Word(Records+2*40+12,f.identity);
        assert(f.Run()==Failure::DuplicateIdentity); Cleared(*f.values);
    }
    {
        Fixture f; f.memory.Word(Records+4,f.String("@unknown_exact_native_key"));
        f.memory.Word(Records+8,0);
        assert(f.Run()==Failure::None);
        assert(f.values->objectives.entries[0].title.str()=="@unknown_exact_native_key");
        assert(f.values->objectives.entries[0].description.str().empty());
        f.memory.Word(Records+4,0); assert(f.Run()==Failure::None);
        assert(f.values->objectives.entries[0].title.str()=="Fields.Fields_Objective_04");
    }
    for (int test=0;test<5;++test) {
        Fixture f;
        if (test==0) f.memory.Word(Records+12,0);
        if (test==1) f.memory.Byte(f.identity,0);
        if (test==2) f.memory.Byte(f.title,0x1f);
        if (test==3) f.memory.Word(Records+4,0xfffffff8);
        if (test==4) for (size_t i=0;i<192;++i) f.memory.Byte(f.title+uint32_t(i),'x');
        assert(f.Run()==Failure::InvalidText); Cleared(*f.values);
    }
    {
        Fixture f; const auto longKey=f.String(std::string(191,'x')); f.memory.Word(Records+4,longKey);
        assert(f.Run()==Failure::None && f.values->objectives.entries[0].title.str().size()==191);
        // A valid terminator at the end of a mapped page must not require the next page.
        f.memory.Add(0x300ff8,8);
        for (uint32_t i=0;i<7;++i) f.memory.Byte(0x300ff8+i,'z');
        f.memory.Word(Records+4,0x300ff8);
        assert(f.Run()==Failure::None && f.values->objectives.entries[0].title.str()=="zzzzzzz");
    }
    {
        Fixture f; int copies=0;
        f.memory.beforeRead=[&](uint32_t address,size_t) { if (address==Records && ++copies==2) f.memory.Word(Records+0x1c,1); };
        assert(f.Run()==Failure::ChangedDuringRead); Cleared(*f.values);
    }
    {
        Fixture f; int copies=0;
        f.memory.beforeRead=[&](uint32_t address,size_t) { if (address==f.title && ++copies==2) f.memory.Byte(f.title,'!'); };
        assert(f.Run()==Failure::InvalidText); Cleared(*f.values);
    }
    {
        Fixture f; int copies=0;
        f.memory.beforeRead=[&](uint32_t address,size_t) { if (address==0x3b9fde30 && ++copies==2) f.memory.Word(0x3b9fde30,Root+16); };
        assert(f.Run()==Failure::ChangedDuringRead); Cleared(*f.values);
    }
    {
        Fixture f;
        auto read=[&](uint32_t address,void* out,size_t count){return f.memory.Read(address,out,count);};
        int guards=0;
        auto guard=[&]{ auto value=f.guard; if (++guards==2) ++value.epoch; return value; };
        assert(c3native::ReadCurrentObjectives(read,guard,true,true,Root,*f.scratch,*f.values)==Failure::ChangedDuringRead);
        Cleared(*f.values);
    }
    {
        Fixture f; f.Extent(65);
        for (size_t i=0;i<65;++i) f.Record(i,i==64?3:1,"id_"+std::to_string(i),"name_"+std::to_string(i),"");
        assert(f.Run()==Failure::None);
        assert(f.values->noninactiveCount==65 && f.values->objectives.count==64 && !f.values->objectives.complete);
        assert(f.values->objectives.entries[0].key==65 && f.values->objectives.entries[0].state==c3::ObjectiveState::Active);
    }
    {
        Fixture f; f.memory.Word(Objectives+0x1c,0); f.memory.Word(Objectives+0x20,0); f.memory.Word(Objectives+0x24,0);
        assert(f.Run()==Failure::None && f.values->objectives.known && f.values->objectives.complete && !f.values->objectives.count);
    }
    std::cout<<"PASS: native objective/level bounded copies, ownership/epoch gates, status/priority, exact English catalog, literal unknown keys, limits and mutation rejection\n";
}

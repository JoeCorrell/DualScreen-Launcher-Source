#include "../src/NativeSuitRead.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
namespace {
struct Range { uint32_t address{}; std::vector<uint8_t> bytes; };
std::vector<uint8_t> File(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary); assert(file);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
struct Memory {
    std::vector<Range> ranges;
    bool operator()(uint32_t address,void* out,size_t bytes) const {
        for(const auto& range:ranges) if(address>=range.address && uint64_t(address)-range.address+bytes<=range.bytes.size()) {
            std::memcpy(out,range.bytes.data()+(address-range.address),bytes); return true;
        }
        return false;
    }
    void Set(uint32_t address,uint32_t value) {
        bool found=false;
        for(auto& range:ranges) if(address>=range.address && uint64_t(address)-range.address+4<=range.bytes.size()) {
            std::memcpy(range.bytes.data()+address-range.address,&value,4); found=true;
        }
        assert(found);
    }
};
}
int main(int argc,char** argv) {
    assert(argc==2); const auto evidence=std::filesystem::path(argv[1])/".work/crysis3/live-205200";
    Memory memory;
    // Include any available ranges: metadata, not hard-coded runtime addresses,
    // determines each immutable capture's location. Not an atomic live snapshot.
    for(const auto& entry:std::filesystem::directory_iterator(evidence)) {
        const auto name=entry.path().filename().string();
        if(name.substr(0,4)!="read") continue;
        const auto id=name.substr(4); const auto jsonFile=entry.path()/("read-"+id+".json");
        if(!std::filesystem::exists(jsonFile)) continue;
        const auto bytes=File(jsonFile); const std::string json(bytes.begin(),bytes.end());
        auto start=json.find("\"address\":"); assert(start!=std::string::npos);
        auto address=static_cast<uint32_t>(std::stoul(json.substr(start+10)));
        memory.ranges.push_back({address,File(entry.path()/("read-"+id+".bin"))});
    }
    uint32_t root{}; assert(memory(0x3b9fde30,&root,4));
    uint32_t actor=0x32cfb470, entity{}; assert(memory(actor+8,&entity,4));
    auto guard=[] { return c3native::ActorFrameGuard{7,41,41,0,true,true}; };
    c3native::NativeSuitModules result{};
    auto run=[&](Memory& mem) { return c3native::ReadSuitModules(mem,root,actor,entity,true,guard(),guard,result); };
    using Failure=c3native::SuitReadFailure;
    assert(run(memory)==Failure::None);
    assert(result.count==16 && result.upgradePoints==4);
    assert(std::string(result.modules[1].id.data())=="Endurance");
    assert(std::string(result.modules[4].id.data())=="BulletProof");
    for(size_t i=0;i<16;++i) {
        assert(result.modules[i].installed==(i==1 || i==4));
        assert(!result.modules[i].purchased && !result.modules[i].maximum);
        assert(!result.modules[i].unlockedKnown); // Native menu cache0 vs authoritative points4.
    }
    auto bad=memory; bad.Set(0x3ba12080,0); assert(run(bad)==Failure::IdentityMismatch && result.count==0);
    bad=memory; bad.Set(0x33399f48+8,17); assert(run(bad)==Failure::UnsupportedLayout);
    bad=memory; bad.Set(actor+8,entity+1); assert(run(bad)==Failure::IdentityMismatch);
    bad=memory; bad.Set(0x33365070,0); assert(run(bad)==Failure::UnsupportedLayout);
    bad=memory; bad.Set(0x33399f54+8,64); assert(run(bad)==Failure::UnsupportedLayout);
    bad=memory; bad.Set(0x33399f54+0x18,0x40000000); assert(run(bad)==Failure::UnsupportedLayout);
    bad=memory; bad.Set(0x33399f48+0xed8,4); assert(run(bad)==Failure::None);
    for(size_t i=0;i<16;++i) assert(result.modules[i].unlockedKnown && result.modules[i].unlocked);
    assert(c3native::ReadSuitModules(memory,root,actor,entity,false,guard(),guard,result)==Failure::GuardUnverified);
    auto changed=[] { return c3native::ActorFrameGuard{8,41,41,0,true,true}; };
    assert(c3native::ReadSuitModules(memory,root,actor,entity,true,guard(),changed,result)==Failure::ChangedDuringRead);
    std::cout<<"PASS: captured 16-module replay, actual active bits and authoritative4 points; stale eligibility cache remains unknown. Rejection/refresh cases pass.\n";
}

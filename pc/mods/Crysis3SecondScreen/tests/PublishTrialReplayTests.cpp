// Offline replay of immutable captured values through the trial's exact reader,
// identity joins, model conversion, mailbox and freshness predicate. This does
// not reproduce runtime scheduling or prove atomicity of the original captures.
#include "../src/ReadOnlyTrial.h"
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
    void Add(uint32_t address,const std::filesystem::path& path) { ranges.push_back({address,File(path)}); }
    bool operator()(uint32_t address,void* out,size_t bytes) const {
        for(const auto& range:ranges) {
            if(address>=range.address && uint64_t(address)-range.address+bytes<=range.bytes.size()) {
                std::memcpy(out,range.bytes.data()+(address-range.address),bytes); return true;
            }
        }
        std::cerr<<"Missing replay range "<<std::hex<<address<<" + "<<bytes<<std::dec<<'\n'; return false;
    }
};
}
int main(int argc,char** argv) {
    assert(argc==2); const std::filesystem::path workspace=argv[1];
    const auto evidence=workspace/".work/crysis3/live-202900";
    Memory memory;
    for(unsigned i=2;i<=19;++i) {
        const auto directory=evidence/("read"+std::to_string(i));
        const auto jsonBytes=File(directory/("read-"+std::to_string(i)+".json"));
        const std::string json(jsonBytes.begin(),jsonBytes.end());
        const auto start=json.find("\"address\":"); assert(start!=std::string::npos);
        const uint32_t address=static_cast<uint32_t>(std::stoul(json.substr(start+10)));
        memory.Add(address,directory/("read-"+std::to_string(i)+".bin"));
    }
    const auto capture=workspace/".work/crysis3/live-190400/capture";
    memory.Add(0x37001000,capture/"loaded-text.bin");
    memory.Add(0x3987f000,capture/"loaded-rdata.bin");
    uint32_t root{}; assert(memory(0x3b9fde30,&root,4));
    auto guard=[] { return c3native::ActorFrameGuard{7,41,41,0,true,true}; };
    c3native::actor_detail::RawActorFrame identity{},after{};
    c3native::actor_detail::RawPhaseFrame phaseIdentity{},phaseAfter{};
    c3native::NativeActorValues actor{}; c3native::NativePhaseValues phase{};
    bool valid=c3native::actor_detail::Once(memory,root,identity)==c3native::ActorReadFailure::None &&
        c3native::actor_detail::PhaseOnce(memory,root,phaseIdentity)==c3native::ActorReadFailure::None &&
        c3native::ReadCurrentActor(memory,guard,root,actor)==c3native::ActorReadFailure::None &&
        c3native::ReadCurrentPhase(memory,guard,root,phase)==c3native::ActorReadFailure::None;
    for(size_t i=0;i<3;++i) valid=valid && identity.identity[i]==phaseIdentity.identity[i];
    assert(valid);
    c3::Snapshot snapshot{}; c3::TrialVitals(snapshot,actor,phase,valid,7,1,1000);
    assert(c3::Current(snapshot,1000));
    valid=valid && c3native::actor_detail::Once(memory,root,after)==c3native::ActorReadFailure::None && identity.Same(after) &&
        c3native::actor_detail::PhaseOnce(memory,root,phaseAfter)==c3native::ActorReadFailure::None && phaseIdentity.Same(phaseAfter);
    assert(valid);
    c3::Mailbox mailbox; assert(mailbox.Publish(snapshot)); c3::Snapshot incoming{}; assert(mailbox.Read(incoming));
    assert(c3::Current(incoming,1100) && incoming.health.known && incoming.health.value==1000);
    assert(incoming.energy.known && incoming.maxEnergy.known && incoming.maxEnergy.value==100);
    assert(!incoming.adapterVerified && incoming.readOnlyTrial && !c3::CanAct(incoming,incoming.toggleCloak,1100));
    assert(!c3::Current(incoming,1751));
    std::cout<<"PASS: immutable live202900 actor/phase replay reaches current read-only mailbox snapshot; health="
        <<incoming.health.value<<" energy="<<incoming.energy.value<<". Scheduling/freshness outside this replay remain untested.\n";
}

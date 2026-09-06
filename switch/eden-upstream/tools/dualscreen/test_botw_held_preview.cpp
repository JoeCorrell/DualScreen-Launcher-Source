// Small standalone test executable; does not build or launch the Android app.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <vector>
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;
using u64 = uint64_t; using s32 = int32_t; using VAddr = uint64_t;
namespace Core::Memory {
struct Memory {
    std::vector<u8> bytes = std::vector<u8>(0x2000000);
    bool mutate = false;
    int pose_reads = 0;
    bool IsValidVirtualAddressRange(VAddr p, size_t n) { return p && p < bytes.size() && n <= bytes.size()-p; }
    bool ReadBlockUnsafe(VAddr p, void* out, size_t n) {
        if (!IsValidVirtualAddressRange(p,n)) return false;
        std::memcpy(out, bytes.data()+p, n);
        if (mutate && p == 0x17000 && ++pose_reads == 2) static_cast<float*>(out)[0] += 1;
        return true;
    }
    template<class T> void put(VAddr p, T v) { assert(IsValidVirtualAddressRange(p,sizeof(T))); std::memcpy(bytes.data()+p,&v,sizeof(T)); }
    void str(VAddr p, const char* s) { std::memcpy(bytes.data()+p,s,std::strlen(s)+1); }
};
}
struct TestSystem {
    Core::Memory::Memory memory;
    bool expected = true;
    u64 GetApplicationProcessProgramID() { return 0x01007EF00011E000ULL; }
    std::pair<u64,u64> GetApplicationProcessMainModule() { return {0x1000,0x1F00000}; }
    auto& ApplicationMemory() { return memory; }
    const TestSystem* ApplicationProcess() { return this; }
    u64 GetProcessId() const { return 1; }
};
struct EmulationSession {
    TestSystem system;
    bool running = true;
    int LockSystemAccess() { return 0; }
    bool IsRunning() { return running; }
    auto& System() { return system; }
};
namespace Common {
std::string HexToString(std::span<const u8> bytes) {
    std::ostringstream out;
    out<<std::hex<<std::setfill('0');
    for(auto b:bytes) out<<std::setw(2)<<unsigned(b);
    return out.str();
}
}
namespace BotwCompanion {
constexpr u64 BotwTitleId = 0x01007EF00011E000ULL;
constexpr VAddr GameDataManagerGotOffset=0x1D66478;
constexpr VAddr GameDataFlagBufferHolderOffset=0xBE0;
constexpr VAddr GameDataFlagBufferGenerationOffset=0xBE8;
bool IsExpectedBuild(TestSystem& s) { return s.expected; }
template<class T> std::optional<T> Read(Core::Memory::Memory& m,VAddr p) {
    T v{}; if (!m.ReadBlockUnsafe(p,&v,sizeof(v))) return {}; return v;
}
std::optional<std::string> ReadCString(Core::Memory::Memory& m,VAddr p) {
    std::array<char,96> s{}; if (!m.ReadBlockUnsafe(p,s.data(),s.size())) return {};
    const auto end=std::find(s.begin(),s.end(),0); if (end==s.end()||end==s.begin()) return {};
    return std::string(s.begin(),end);
}
std::string JsonString(const std::string& s) { return '"'+s+'"'; }
// The runner extracts these exact production decoders, not mock implementations.
#include "botw_test_getters.inc"
#include "../../src/android/app/src/main/jni/botw_held_preview.inc"
#include "../../src/android/app/src/main/jni/botw_map_discovery.inc"
}
int main() {
    EmulationSession s;
    auto& m=s.system.memory;
    auto p=[&](u64 a,u64 v){m.put(a,v);};
    auto link=[&](u64 a,u64 d,u64 actor,u32 id){p(a,d);m.put(a+8,id);m.put(d+0x40,id);p(d+0x48,actor);};
    constexpr u64 player=0x10000, manager=0x12000, carry=0x13000, data=0x14000;
    p(0x1000+0x1E13ED0,0x11000);p(0x11060,player);
    m.put(player+0x64,u8{1});m.put(player+0x60,u32{2});m.put(player+0xD11,u8{1});
    p(0x1000+0x1E0C828,manager);link(manager+0x300,0x18000,carry,3);p(manager+0x310,data);
    link(data+0x708,0x18000,carry,3);p(player+0x88,carry);p(carry+0x80,player);
    m.put(carry+0x92C,u8{1});m.put(carry+0x970,u32{2});p(data+0x640,0x19000);
    p(player+0x4E0,0x15000);p(0x15000,0x1000+0x1D0FA48);m.put(0x150A4,u16{1});
    p(0x15030,0x16000);p(0x16000,0x20000);p(0x20000,0x1000+0x1D10738);
    p(0x20118,0x21000);p(0x21000,0x22000);m.put(0x2203C,u16{2});
    p(0x22018,0x23000);p(0x21020,0x17000);
    p(0x23000,0x24000);m.str(0x24002,"Root");m.put(0x2301A,u16{0xffff});
    p(0x23050,0x24100);m.str(0x24102,"Weapon_R");m.put(0x2306A,u16{0});
    const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const std::array<float,12> row{1,0,0,0,0,1,0,0,0,0,1,0};
    m.put(0x15058,row);m.put(0x17000,identity);m.put(0x17040,identity);
    for (u32 i=0;i<5;++i) {
        const u64 slot=0x25000+i*0x200, actor=0x30000+i*0x1000, model=0x40000+i*0x1000;
        p(0x19000+i*8,slot);link(slot+0x30,0x28000+i*0x100,actor,i+4);p(slot+0x118,data);
        m.put(actor+0x60,i+4);p(actor+0x10,0x29000+i*0x100);m.str(0x29000+i*0x100,i%2?"Animal_Insect_A":"Item_Fruit_A");
        p(actor+0x4E0,model);m.put(model+0x58,row);
        m.put(model+0x88,std::array<float,6>{.5f,.75f,1.f,.5f,1.f,1.f});
    }
    auto ok=[&]{return BotwCompanion::GetHeldPreview(s).find("\"status\":\"ok\"")!=std::string::npos;};
    for(s32 n=1;n<=5;++n){m.put(data+0x638,n);assert(ok());}
    assert(BotwCompanion::GetHeldPreview(s).find("\"world\":[0.25,0,0,0,0,0.75,0,0,0,0,1,0,0,0,0,1]")!=std::string::npos);
    m.put(0x40088,0.f);assert(!ok());m.put(0x40088,.5f);
    m.put(data+0x638,s32{6});assert(!ok());m.put(data+0x638,s32{2});
    m.put(player+0xD11,u8{0});assert(BotwCompanion::GetHeldPreview(s)=="{\"status\":\"ok\",\"items\":[]}");
    m.put(player+0xD11,u8{1});m.put(player+0x64,u8{2});assert(!ok());m.put(player+0x64,u8{1});
    m.put(0x28040,u32{99});assert(!ok());m.put(0x28040,u32{4});
    m.put(0x2306A,u16{1});assert(!ok());m.put(0x2306A,u16{0});
    m.mutate=true;assert(!ok());m.mutate=false;
    s.system.expected=false;assert(!ok());s.system.expected=true;
    s.running=false;assert(!ok());s.running=true;
    const auto before=m.bytes;assert(ok());assert(before==m.bytes);
    std::cout<<"PASS: 1..5 native held slots, duplicate materials, insects, release, stale links, invalid hierarchy, torn pose, build/lifecycle guards, read-only memory\n";
    p(0x1000+BotwCompanion::GameDataManagerGotOffset,0x60000);p(0x60000,0x61000);
    p(0x61BE0,0x62000);p(0x62000,0x63000);m.put(0x61BE8,u32{1});
    p(0x63010,0x64000);m.put(0x63008,u32{1});p(0x64000,0x65000);
    const auto tower=std::find_if(BotwCompanion::BotwMapFlags.begin(),BotwCompanion::BotwMapFlags.end(),
        [](auto v){return v.name=="MapTower_07";});
    assert(tower!=BotwCompanion::BotwMapFlags.end());
    m.put(0x6500C,tower->hash);m.put(0x6500B,u8{1});
    p(0x1000+0x1D66568,0x66000);p(0x66000,0x67000);p(0x671F0,0x68000);
    m.put(0x68000,std::array<u32,4>{0x00112233,2,800,0});
    m.put(0x68010,u32{0});m.put(0x68014,u32{2});m.put(0x68018,u16{6});m.put(0x6801A,u16{10000});
    // Real getter instruction shapes: getHash, s32 getValue, vector getValueRef.
    m.put(0x70000,std::array<u32,2>{0xB9400C00,0xD65F03C0}); // ldr w0,[x0,#12]; ret
    m.put(0x70010,std::array<u32,2>{0xB9401000,0xD65F03C0}); // ldr w0,[x0,#16]; ret
    m.put(0x70020,std::array<u32,2>{0x91004000,0xD65F03C0}); // add x0,x0,#16; ret
    p(0x71010,0x70000);p(0x71088,0x70010);p(0x71110,0x70000);p(0x71190,0x70020);
    // Save audit values: these are discovered s32 locations, including even
    // values (not bool bit0). Leave a fourth location at zero/undiscovered.
    std::vector<std::pair<u32,s32>> locations;
    for (const auto& flag:BotwCompanion::BotwMapFlags) {
        if (flag.name=="Location_Hateno") locations.emplace_back(flag.hash,74);
        if (flag.name=="Location_Kakariko") locations.emplace_back(flag.hash,11);
        if (flag.name=="Location_MapTower07") locations.emplace_back(flag.hash,6);
        if (flag.name=="Location_Rito") locations.emplace_back(flag.hash,0);
    }
    assert(locations.size()==4);std::sort(locations.begin(),locations.end());
    m.put(0x63018,u32{4});p(0x63020,0x76000);
    for(size_t i=0;i<locations.size();++i) {
        const auto flag=0x77000+i*0x100;
        p(0x76000+i*8,flag);p(flag,0x71000);
        m.put(flag+0xC,locations[i].first);m.put(flag+0x10,locations[i].second);
    }
    m.put(0x630A8,u32{1});p(0x630B0,0x72000);p(0x72000,0x72100);
    m.put(0x72100,u32{2});p(0x72108,0x72200);
    m.put(0x63108,u32{1});p(0x63110,0x73000);p(0x73000,0x73100);
    m.put(0x73100,u32{2});p(0x73108,0x73200);
    for(u32 i=0;i<2;++i) {
        const u64 number=0x74000+i*0x100, position=0x75000+i*0x100;
        p(0x72200+i*8,number);p(0x73200+i*8,position);
        p(number,0x71000);m.put(number+0xC,u32{0x9383490E});m.put(number+0x10,s32{37+static_cast<s32>(i)});
        p(position,0x71100);m.put(position+0xC,u32{0xEA9DEF3F});
        m.put(position+0x10,std::array<float,3>{123,100,-321});
    }
    auto discovery=BotwCompanion::GetMapDiscovery(s,"");
    assert(discovery.find("MapTower_07")!=std::string::npos && discovery.find("regions")!=std::string::npos);
    for (const auto* name:{"Location_Hateno","Location_Kakariko","Location_MapTower07"})
        assert(discovery.find(name)!=std::string::npos);
    assert(discovery.find("Location_Rito")==std::string::npos);
    assert(discovery.find("\"slot\":0,\"icon\":37,\"x\":123,\"z\":-321")!=std::string::npos);
    assert(discovery.find("\"slot\":1,\"icon\":38")!=std::string::npos);
    m.put(0x74010,s32{-1});m.put(0x75110,std::array<float,3>{-100000,0,0});
    assert(BotwCompanion::GetMapDiscovery(s,"").find("\"userMarkers\":[]")!=std::string::npos);
    m.put(0x74010,s32{27});
    assert(BotwCompanion::GetMapDiscovery(s,"").find("\"icon\":27")!=std::string::npos);
    m.put(0x74010,s32{44}); // non-pin native icons need their own progression checks
    assert(BotwCompanion::GetMapDiscovery(s,"").find("\"userMarkers\":[]")!=std::string::npos);
    assert(BotwCompanion::GetMapDiscovery(s,"1:425984").find("\"regions\"")==std::string::npos);
    m.put(0x6500B,u8{0});assert(BotwCompanion::GetMapDiscovery(s,"").find("MapTower_07")==std::string::npos);
    m.put(0x68000,u32{0});assert(BotwCompanion::GetMapDiscovery(s,"").find("unavailable")!=std::string::npos);
    std::cout<<"PASS: native map flags, region revisions, saved pins/stamps, removed/invalid marker rejection\n";
}

#pragma once
#include "NativeActorRead.h"
#include "Model.h"
#include <cstdio>

namespace c3native {
struct StatsFingerprint { uint32_t address; size_t size; const char* sha256; };
inline constexpr StatsFingerprint NativeStatsProfile[]{
    {0x3818bc80,76,"9dc3a7db338feb1f926d055333befaa84d8ee58247deb8ce75c483268326a287"},
    {0x3841d8d0,99,"a09746d3af2b2d6f3c14b2abe7985afeae422ecfa218e6aaf08b1a4e86229d42"},
    {0x3841db10,32,"5ad8cba13853b8e6074649d4b51d0a65a8974765c2873a933ded0a0155e9b4e3"},
    {0x3840ca00,16,"d937380522c3a44cefe1bed521500fb7f6dd81b1d9068da3be8ff407a6738b95"},
    {0x3840ca10,16,"70834c7b36ccf83503cce81f5450906ecd620091c3794366059e8a05a1d5d726"},
    {0x3840cfb0,16,"afc60669d2290c7819f906621fdf6e8ef40151221a9fe484a85a09316d2fabd3"},
    {0x3840cfc0,16,"0635b8ca24dd0a5c768fa263b946c7682a52caddbc736857c13ea82eba353a9e"},
    {0x39a1a6b4,32,"c946b7900b339dbc2a7444842ce6419684c2d1c7c16ac11908bc68932ad2f078"},
    {0x3b826930,1132,"8d86b987c6ec32e866f477d28baaa550c55cc93658bea39b1a64bbdccc34990f"}
};
enum class StatsReadFailure { None, GuardUnverified, ReadFailed, OwnerMismatch, ChangedDuringRead, InvalidValue };
namespace stats_detail {
struct Raw {
    uint32_t root{},owner{},actor{},entity{};
    std::array<uint32_t,283> words{};
    bool Same(const Raw& other)const{return root==other.root&&owner==other.owner&&actor==other.actor&&entity==other.entity&&words==other.words;}
};
template<class Read> StatsReadFailure Once(Read& read,uint32_t root,uint32_t actor,uint32_t entity,Raw& out){
    using actor_detail::At;
    uint32_t globalRoot{},rootVt{},globalOwner{},ownerVt{},actorVt{},actorId{};
    if(!At(read,actor_detail::RootSlot,0,globalRoot)||!At(read,root,0,rootVt)||
       !At(read,root,0xd4,out.owner)||!At(read,0x3ba0ba50,0,globalOwner)||
       !At(read,out.owner,0,ownerVt)||!At(read,actor,0,actorVt)||!At(read,actor,8,actorId))
        return StatsReadFailure::ReadFailed;
    if(!entity||globalRoot!=root||rootVt!=actor_detail::RootVtable||!out.owner||out.owner!=globalOwner||
       ownerVt!=0x39a1a6b4||actorVt!=actor_detail::ActorVtable||actorId!=entity)return StatsReadFailure::OwnerMismatch;
    if(!At(read,out.owner,0x22c,out.words))return StatsReadFailure::ReadFailed;
    out.root=root;out.actor=actor;out.entity=entity;return StatsReadFailure::None;
}
struct Entry { uint32_t index; bool floating; const char* label; const char* unit; };
// Index -> name -> offset verified by audit_stat_tables.py and live read6.
// No mission-time claim or inferred total kills. Raw TimePlayed is not emitted:
// live read5/read8 proved that stored field does NOT advance every frame.
inline constexpr Entry Entries[]{
    {0,false,"TACTICAL KILLS",""},{1,false,"ARMOR KILLS",""},{2,false,"CLOAK KILLS",""},
    {37,false,"STEALTH KILLS",""},{62,false,"HEADSHOT KILLS",""},{59,false,"MELEE TAKEDOWNS",""},
    {50,false,"OBJECTS SCANNED",""},{110,false,"ENTITIES TAGGED",""},
    {113,false,"HACKING ATTEMPTS",""},{114,false,"HACKS COMPLETED",""},
    {117,false,"ARMOR ACTIVATIONS",""},
    {157,false,"TACTICAL SUIT TIME","sec"},{158,false,"ARMOR SUIT TIME","sec"},{159,false,"CLOAK SUIT TIME","sec"},
    {1,true,"DISTANCE RUN","game units"},{2,true,"DISTANCE SPRINTED","game units"},
    {17,true,"DISTANCE SWUM","game units"}
};
inline StatsReadFailure Decode(const Raw& raw,c3::List<c3::Statistic,48>& output){
    std::array<float,std::size(Entries)> values{};size_t index=0;
    for(const auto& entry:Entries){
        float value{};
        if(entry.floating)std::memcpy(&value,&raw.words[258+entry.index],4);
        else { if(raw.words[entry.index]>16777216)return StatsReadFailure::InvalidValue;value=float(raw.words[entry.index]); }
        if(!std::isfinite(value)||value<0||value>1e9f)return StatsReadFailure::InvalidValue;
        values[index++]=value;
    }
    output={};index=0;
    for(const auto& entry:Entries){
        auto& row=output.entries[output.count++];
        std::snprintf(row.name.bytes.data(),row.name.bytes.size(),"%s",entry.label);
        std::snprintf(row.scope.bytes.data(),row.scope.bytes.size(),"GAME COUNTER");
        std::snprintf(row.unit.bytes.data(),row.unit.bytes.size(),"%s",entry.unit);
        row.value={values[index++],true};
    }
    output.known=output.complete=true;return StatsReadFailure::None;
}
}
template<class Read,class GuardNow>
StatsReadFailure ReadStatistics(Read& read,uint32_t root,uint32_t actor,uint32_t entity,bool profile,
    const ActorFrameGuard& entry,GuardNow&& guardNow,c3::List<c3::Statistic,48>& out){
    out={};if(!profile||!entry.Usable())return StatsReadFailure::GuardUnverified;
    stats_detail::Raw first{},last{};
    auto status=stats_detail::Once(read,root,actor,entity,first);if(status!=StatsReadFailure::None)return status;
    status=stats_detail::Once(read,root,actor,entity,last);if(status!=StatsReadFailure::None)return status;
    if(!first.Same(last)||!entry.Same(guardNow()))return StatsReadFailure::ChangedDuringRead;
    return stats_detail::Decode(first,out);
}
} // namespace c3native

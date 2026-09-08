#pragma once
#include "Model.h"
#include "NativeMissionRead.h"
#include "NativeObjectiveCatalog.h"
#include <array>
#include <cstring>
#include <string_view>

namespace c3native {
enum class ObjectiveReadFailure {
    None, GuardUnverified, ReadFailed, UnsupportedLayout, ChangedDuringRead,
    InvalidVector, InvalidText, InvalidRecord, DuplicateIdentity
};
struct NativeObjectiveValues {
    c3::Text levelName{}, levelPath{}; // Actual level identifiers, not invented mission titles.
    c3::Text missionTitle{}, location{}; // Exact English asset text; unknown title falls back to raw levelName.
    c3::List<c3::Objective,64> objectives{};
    uint32_t nativeRecordCount{}, noninactiveCount{};
};
namespace objective_detail {
constexpr uint32_t RootSlot=0x3b9fde30;
constexpr size_t MaxRecords=256, RecordBytes=c3::mission_research::RecordBytes;
using RawRecords=std::array<unsigned char,MaxRecords*RecordBytes>;
struct Chain {
    uint32_t root{}, framework{}, levelSystem{}, currentLevel{}, objectiveSystem{};
    std::array<uint32_t,4> level{};
    std::array<uint32_t,3> vector{};
    bool Same(const Chain& b) const noexcept {
        return root==b.root && framework==b.framework && levelSystem==b.levelSystem &&
            currentLevel==b.currentLevel && objectiveSystem==b.objectiveSystem && level==b.level && vector==b.vector;
    }
};
struct RawObjective { c3::Text identity{}, title{}, description{}; uint32_t index{}; };
struct Frame {
    Chain chain{};
    RawRecords records{};
    c3::Text levelName{}, levelPath{};
    std::array<RawObjective,64> shown{};
    size_t count{}, shownCount{}, noninactiveCount{};
    bool Same(const Frame& b) const noexcept {
        if (!chain.Same(b.chain) || count!=b.count || shownCount!=b.shownCount || noninactiveCount!=b.noninactiveCount ||
            records!=b.records || levelName.bytes!=b.levelName.bytes || levelPath.bytes!=b.levelPath.bytes) return false;
        for (size_t i=0;i<shownCount;++i)
            if (shown[i].index!=b.shown[i].index || shown[i].identity.bytes!=b.shown[i].identity.bytes ||
                shown[i].title.bytes!=b.shown[i].title.bytes || shown[i].description.bytes!=b.shown[i].description.bytes) return false;
        return true;
    }
};
template<class Read> bool Bytes(Read& read,uint32_t base,uint32_t offset,void* out,size_t count) {
    const uint64_t address=uint64_t(base)+offset;
    if (base<0x10000 || !count || count>4096 || address>UINT32_MAX || address+count>0x100000000ull) return false;
    return read(uint32_t(address),out,count);
}
template<class Read,class T> bool At(Read& read,uint32_t base,uint32_t offset,T& out) {
    return Bytes(read,base,offset,&out,sizeof(out));
}
template<class Read> ObjectiveReadFailure ReadChain(Read& read,uint32_t expectedRoot,Chain& chain) {
    Chain next{}; uint32_t vt{};
    if (!At(read,RootSlot,0,next.root)) return ObjectiveReadFailure::ReadFailed;
    if (!next.root || next.root!=expectedRoot) return ObjectiveReadFailure::ChangedDuringRead;
    if (!At(read,next.root,0,vt)) return ObjectiveReadFailure::ReadFailed;
    if (vt!=0x399bef80) return ObjectiveReadFailure::UnsupportedLayout;
    if (!At(read,next.root,0x30,next.framework) || !At(read,next.root,0x104,next.objectiveSystem) ||
        !At(read,next.framework,0,vt)) return ObjectiveReadFailure::ReadFailed;
    if (vt!=0x398ba5e0) return ObjectiveReadFailure::UnsupportedLayout;
    if (!At(read,next.framework,0x4ac,next.levelSystem) || !At(read,next.levelSystem,0,vt)) return ObjectiveReadFailure::ReadFailed;
    if (vt!=0x398868a0) return ObjectiveReadFailure::UnsupportedLayout;
    if (!At(read,next.levelSystem,0x1c,next.currentLevel) || !At(read,next.currentLevel,0,next.level)) return ObjectiveReadFailure::ReadFailed;
    c3::mission_research::LevelIdentity info{};
    if (!c3::mission_research::DecodeLevelHeader(next.level.data(),sizeof(next.level),info)) return ObjectiveReadFailure::UnsupportedLayout;
    if (!At(read,next.objectiveSystem,0,vt) || !At(read,next.objectiveSystem,0x1c,next.vector)) return ObjectiveReadFailure::ReadFailed;
    if (vt!=0x399f32c0) return ObjectiveReadFailure::UnsupportedLayout;
    const uint32_t first=next.vector[0],last=next.vector[1],capacity=next.vector[2];
    if ((first|last|capacity)&3 || last<first || capacity<last ||
        (last-first)%RecordBytes || (capacity-first)%RecordBytes || last-first>MaxRecords*RecordBytes ||
        capacity-first>4096*RecordBytes || (first && first<0x10000) || (!first && (last || capacity)))
        return ObjectiveReadFailure::InvalidVector;
    chain=next; return ObjectiveReadFailure::None;
}
template<class Read> bool Text(Read& read,uint32_t address,c3::Text& text,bool allowEmpty) {
    text={};
    if (!address) return allowEmpty;
    size_t used=0;
    while (used<text.bytes.size()) {
        // Never straddle a page boundary simply to find a nearby terminator.
        const uint64_t cursor=uint64_t(address)+used;
        if (cursor>UINT32_MAX) return false;
        const size_t count=std::min<size_t>({16,text.bytes.size()-used,4096-size_t(cursor&4095)});
        std::array<unsigned char,16> block{};
        if (!Bytes(read,address,uint32_t(used),block.data(),count)) return false;
        for (size_t i=0;i<count;++i) {
            if (!block[i]) {
                if (!used && !allowEmpty) return false;
                std::array<char,192> again{};
                // Compare only bytes through NUL, not unrelated string-pool tail.
                return Bytes(read,address,0,again.data(),used+1) &&
                    std::memcmp(text.bytes.data(),again.data(),used+1)==0;
            }
            if (block[i]<0x20 || block[i]>0x7e) return false; // Exact assets use ASCII keys/level paths.
            text.bytes[used++]=char(block[i]);
        }
    }
    text={}; return false; // No silent truncation of native identity/localization keys.
}
template<class Read> bool Vector(Read& read,const Chain& chain,RawRecords& records) {
    const size_t total=chain.vector[1]-chain.vector[0];
    for (size_t at=0;at<total;) {
        const size_t count=std::min<size_t>(4096,total-at);
        if (!Bytes(read,chain.vector[0],uint32_t(at),records.data()+at,count)) return false;
        at+=count;
    }
    return true;
}
template<class Read> ObjectiveReadFailure Once(Read& read,uint32_t root,Frame& frame,RawRecords& verify) {
    frame={}; verify={};
    auto failure=ReadChain(read,root,frame.chain);
    if (failure!=ObjectiveReadFailure::None) return failure;
    if (!Text(read,frame.chain.level[2],frame.levelName,false) || !Text(read,frame.chain.level[3],frame.levelPath,false))
        return ObjectiveReadFailure::InvalidText;
    if (!Vector(read,frame.chain,frame.records)) return ObjectiveReadFailure::ReadFailed;
    frame.count=(frame.chain.vector[1]-frame.chain.vector[0])/RecordBytes;
    for (size_t i=0;i<frame.count;++i) {
        c3::mission_research::Record record{};
        const auto* raw=frame.records.data()+i*RecordBytes;
        // All 99 records in fresh 205200/read24 retain the containing objective
        // system at +0. Enforce that back-reference even for inactive records.
        if (c3::mission_research::Word(raw)!=frame.chain.objectiveSystem ||
            !c3::mission_research::DecodeRecord(raw,RecordBytes,record)) return ObjectiveReadFailure::InvalidRecord;
        if (record.status!=c3::mission_research::NativeStatus::Deactivated) ++frame.noninactiveCount;
    }
    // Deliberate presentation priority: live tasks, failed tasks, then completed.
    // This is not a claim about the engine's record order or mission filtering.
    for (const auto status:{c3::mission_research::NativeStatus::Active,c3::mission_research::NativeStatus::Failed,c3::mission_research::NativeStatus::Complete}) {
        for (size_t i=0;i<frame.count && frame.shownCount<frame.shown.size();++i) {
            c3::mission_research::Record record{};
            if (!c3::mission_research::DecodeRecord(frame.records.data()+i*RecordBytes,RecordBytes,record)) return ObjectiveReadFailure::InvalidRecord;
            if (record.status!=status) continue;
            auto& shown=frame.shown[frame.shownCount]; shown.index=uint32_t(i);
            if (!Text(read,record.identityAddress,shown.identity,false) ||
                !Text(read,record.titleKeyAddress,shown.title,true) || !Text(read,record.descriptionKeyAddress,shown.description,true))
                return ObjectiveReadFailure::InvalidText;
            for (size_t previous=0;previous<frame.shownCount;++previous)
                if (frame.shown[previous].identity.bytes==shown.identity.bytes) return ObjectiveReadFailure::DuplicateIdentity;
            ++frame.shownCount;
        }
    }
    Chain again{};
    failure=ReadChain(read,root,again);
    if (failure!=ObjectiveReadFailure::None) return failure;
    if (!again.Same(frame.chain)) return ObjectiveReadFailure::ChangedDuringRead;
    if (!Vector(read,again,verify)) return ObjectiveReadFailure::ReadFailed;
    if (verify!=frame.records) return ObjectiveReadFailure::ChangedDuringRead;
    return ObjectiveReadFailure::None;
}
inline c3::Text English(const c3::Text& original) noexcept {
    const auto end=std::find(original.bytes.begin(),original.bytes.end(),'\0');
    std::string_view key(original.bytes.data(),size_t(end-original.bytes.begin()));
    if (!key.empty() && key.front()=='@') key.remove_prefix(1);
    size_t first=0,last=c3::objective_catalog::Count;
    while (first<last) {
        const size_t middle=first+(last-first)/2;
        if (c3::objective_catalog::Entries[middle].key<key) first=middle+1; else last=middle;
    }
    if (first==c3::objective_catalog::Count || c3::objective_catalog::Entries[first].key!=key) return original;
    const auto english=c3::objective_catalog::Entries[first].english;
    c3::Text out{};
    if (english.size()>=out.bytes.size()) return original;
    std::memcpy(out.bytes.data(),english.data(),english.size()); return out;
}
inline void LevelEnglish(const c3::Text& level,c3::Text& title,c3::Text& location) noexcept {
    title=level; location={};
    c3::Text folded=level;
    for (auto& byte:folded.bytes) if (byte>='A' && byte<='Z') byte=char(byte-'A'+'a');
    const auto end=std::find(folded.bytes.begin(),folded.bytes.end(),'\0');
    const std::string_view name(folded.bytes.data(),size_t(end-folded.bytes.begin()));
    for (const auto& entry:c3::objective_catalog::Levels) if (entry.level==name) {
        if (entry.title.size()>=title.bytes.size() || entry.location.size()>=location.bytes.size()) return;
        title={}; std::memcpy(title.bytes.data(),entry.title.data(),entry.title.size());
        std::memcpy(location.bytes.data(),entry.location.data(),entry.location.size());
        return;
    }
}
} // namespace objective_detail

// Allocate once outside the game callback (e.g. in the host's reader state).
// Do not put these large temporary buffers on the native callback stack or share
// one scratch object between concurrent readers. No persistent engine pointers.
struct NativeObjectiveScratch {
    objective_detail::Frame first{}, second{};
    objective_detail::RawRecords verify{};
};

// Read and Guard are caller-owned. Guard must implement the existing
// ActorFrameGuard Usable()/Same() contract (epoch, attested callback thread,
// no nested system event). exactObjectiveProfile comes from the separate hash
// manifest; phaseEligible must come from verified gameplay/paused lifecycle.
// No engine calls, memory writes, actions, native pointers in the result, or
// adapterVerified changes. Equal copies reject observed change, not ABA reuse.
template<class Read,class Guard> ObjectiveReadFailure ReadCurrentObjectives(
    Read&& read,Guard&& guard,bool exactObjectiveProfile,bool phaseEligible,uint32_t expectedRoot,
    NativeObjectiveScratch& scratch,NativeObjectiveValues& values) {
    values={};
    const auto before=guard();
    if (!exactObjectiveProfile || !phaseEligible || !before.Usable()) return ObjectiveReadFailure::GuardUnverified;
    auto failure=objective_detail::Once(read,expectedRoot,scratch.first,scratch.verify);
    if (failure!=ObjectiveReadFailure::None) return failure;
    failure=objective_detail::Once(read,expectedRoot,scratch.second,scratch.verify);
    if (failure!=ObjectiveReadFailure::None) return failure;
    if (!before.Same(guard()) || !scratch.first.Same(scratch.second)) return ObjectiveReadFailure::ChangedDuringRead;
    const auto& frame=scratch.second;
    values.levelName=frame.levelName; values.levelPath=frame.levelPath;
    objective_detail::LevelEnglish(frame.levelName,values.missionTitle,values.location);
    values.nativeRecordCount=uint32_t(frame.count); values.noninactiveCount=uint32_t(frame.noninactiveCount);
    values.objectives.known=true; values.objectives.complete=frame.shownCount==frame.noninactiveCount;
    values.objectives.count=uint32_t(frame.shownCount);
    for (size_t i=0;i<frame.shownCount;++i) {
        const auto& source=frame.shown[i];
        c3::mission_research::Record record{};
        c3::mission_research::DecodeRecord(frame.records.data()+source.index*objective_detail::RecordBytes,objective_detail::RecordBytes,record);
        auto& target=values.objectives.entries[i];
        target.key=uint64_t(source.index)+1; // Generation-scoped collection slot, never a game address.
        target.title=objective_detail::English(source.title.bytes[0]?source.title:source.identity);
        target.description=objective_detail::English(source.description);
        target.optional={record.secondary,true};
        switch (record.status) {
        case c3::mission_research::NativeStatus::Active: target.state=c3::ObjectiveState::Active; break;
        case c3::mission_research::NativeStatus::Failed: target.state=c3::ObjectiveState::Failed; break;
        case c3::mission_research::NativeStatus::Complete: target.state=c3::ObjectiveState::Complete; break;
        default: break;
        }
    }
    return ObjectiveReadFailure::None;
}
} // namespace c3native

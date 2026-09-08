#pragma once
#include "State.h"

namespace ac2ds {
inline uint64_t SequenceKey(const Memory& memory) {
    // Native story and side-activity identities use separate high-word domains.
    // Titles are localized presentation, never a stable identity.
    return std::max(uint64_t(1),memory.id>>32);
}
struct MissionSequence {
    uint64_t id{};
    std::wstring title;
    size_t visible{}, completed{};
    bool active{};
};
inline std::vector<MissionSequence> MissionSequences(const Snapshot& snapshot) {
    std::vector<MissionSequence> result;
    if(!(snapshot.capabilities&Missions))return result;
    for(const auto& memory:snapshot.memories){
        if(!memory.unlocked && !memory.exposed)continue;
        auto id=SequenceKey(memory);
        auto row=std::find_if(result.begin(),result.end(),[&](const MissionSequence& value){return value.id==id;});
        if(row==result.end()){
            result.push_back({id,memory.sequence,0,0,false});row=std::prev(result.end());
        }
        ++row->visible;if(memory.complete && memory.unlocked)++row->completed;
        row->active|=memory.active && memory.unlocked;
    }
    return result;
}
inline std::vector<const Memory*> SequenceMemories(const Snapshot& snapshot,uint64_t sequence) {
    std::vector<const Memory*> result;
    if(snapshot.capabilities&Missions)for(const auto& memory:snapshot.memories)
        if((memory.unlocked || memory.exposed) && SequenceKey(memory)==sequence)result.push_back(&memory);
    return result;
}
}

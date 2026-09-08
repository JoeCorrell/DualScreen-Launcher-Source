#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include <algorithm>
namespace ac2ds {
struct CharacterFrameTag {
    uint64_t generation{},frame{},pass{};
    bool operator==(const CharacterFrameTag& other)const{
        return generation==other.generation && frame==other.frame && pass==other.pass;
    }
};
// Keys represent instance + material section from a verified native pass.
// A mesh can have several sections; counting meshes alone permits incomplete
// robes/armor to be published. No previous-frame sections may fill a gap.
class CharacterFramePlan {
    CharacterFrameTag tag{};
    std::vector<uint64_t> keys;
    std::vector<bool> captured;
    bool valid{};
public:
    void Reset(){valid=false;tag={};keys.clear();captured.clear();}
    bool Begin(CharacterFrameTag value,const std::vector<uint64_t>& expected){
        Reset();
        if(!value.generation || !value.frame || !value.pass || expected.empty() || expected.size()>256)return false;
        auto unique=expected;std::sort(unique.begin(),unique.end());
        if(!unique.front() || std::adjacent_find(unique.begin(),unique.end())!=unique.end())return false;
        tag=value;keys=expected;captured.assign(keys.size(),false);valid=true;return true;
    }
    std::optional<size_t> Slot(CharacterFrameTag value,uint64_t key)const{
        if(!valid || !(value==tag))return {};
        auto found=std::find(keys.begin(),keys.end(),key);if(found==keys.end())return {};
        return size_t(found-keys.begin());
    }
    bool Accept(CharacterFrameTag value,uint64_t key,bool success){
        auto slot=Slot(value,key);if(!slot)return false; // Unrelated character/pass is ignored.
        if(!success || captured[*slot]){Reset();return false;}
        captured[*slot]=true;return true;
    }
    bool Complete(CharacterFrameTag value)const{
        return valid && value==tag && std::all_of(captured.begin(),captured.end(),[](bool x){return x;});
    }
};
}

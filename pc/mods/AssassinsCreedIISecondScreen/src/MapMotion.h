#pragma once
#include "State.h"
namespace ac2ds {
struct MapPose { std::optional<float> x,y,heading; };
class MapMotion {
    MapPose from_,target_;
    uint64_t generation_{},player_{},world_{},sample_{},start_{},duration_{};
public:
    MapPose At(uint64_t now) const {
        if(!duration_ || now>=start_+duration_ || !from_.x || !from_.y || !target_.x || !target_.y)return target_;
        float t=now<=start_?0:float(now-start_)/duration_;
        MapPose p{*from_.x+(*target_.x-*from_.x)*t,*from_.y+(*target_.y-*from_.y)*t,target_.heading};
        if(from_.heading && target_.heading) {
            constexpr float pi=3.14159265358979323846f;
            float delta=std::remainder(*target_.heading-*from_.heading,2*pi);
            p.heading=*from_.heading+delta*t;
        }
        return p;
    }
    MapPose Update(const Snapshot& s,uint64_t now) {
        MapPose next{s.mapX,s.mapY,s.heading};
        if(!(s.capabilities&Map) || !(s.capabilities&Player) || !s.map)next={};
        bool transition=s.generation!=generation_ || s.playerIdentity!=player_ || s.worldIdentity!=world_ ||
            s.phase!=Phase::Playing || s.sampledAt<sample_ || now<start_;
        if(transition || next.x!=target_.x || next.y!=target_.y || next.heading!=target_.heading) {
            auto current=At(now);duration_=0;
            // Only interpolate between observed points in the same world.
            // Teleports/loads and missing native values snap or clear at once.
            if(!transition && current.x && current.y && next.x && next.y && sample_ && s.sampledAt>sample_ &&
                std::hypot(*next.x-*current.x,*next.y-*current.y)<.08f)
                duration_=std::min<uint64_t>(250,s.sampledAt-sample_);
            from_=current;target_=next;start_=now;
        }
        generation_=s.generation;player_=s.playerIdentity;world_=s.worldIdentity;sample_=s.sampledAt;
        return At(now);
    }
};
}

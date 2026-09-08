#pragma once
#include "Touch.h"
#include <cstdint>
namespace ac2ds {
// Gesture ownership is decided once. Map-interior drags belong to the map;
// vertical list drags never turn into navigation halfway through scrolling.
class PageSwipe {
    float x_{},y_{};uint64_t started_{},suppressUntil_{};
    bool tracking_{},eligible_{};int axis_{};
public:
    void Reset(){tracking_=eligible_=false;axis_=0;suppressUntil_=0;}
    void Down(float x,float y,bool map,uint64_t now){
        tracking_=true;eligible_=!map || x<=.045f || x>=.955f || y<.08f;
        x_=x;y_=y;started_=now;axis_=0;suppressUntil_=0;
    }
    bool Move(float x,float y){
        if(!tracking_ || !eligible_)return false;
        float dx=(x-x_)*1240,dy=(y-y_)*1080;
        if(!axis_ && std::max(std::abs(dx),std::abs(dy))>=16)
            axis_=std::abs(dx)>std::abs(dy)*1.4f?1:2;
        return axis_==1;
    }
    int Up(float x,float y,uint64_t now){
        if(!tracking_)return 0;
        bool horizontal=Move(x,y);tracking_=false;
        if(!horizontal)return 0;
        suppressUntil_=now+350;
        float dx=(x-x_)*1240,dy=(y-y_)*1080;
        if(now<started_ || now-started_>1500 || std::abs(dx)<110 || std::abs(dx)<std::abs(dy)*1.4f)return 0;
        return dx<0?1:-1;
    }
    void Cancel(uint64_t now){tracking_=eligible_=false;axis_=0;suppressUntil_=now+350;}
    bool SuppressTap(uint64_t now)const{return (tracking_ && axis_==1) || now<suppressUntil_;}
};
inline float PageSlideFraction(uint64_t started,uint64_t now){
    float t=now<started?0.f:std::min(1.f,float(now-started)/220.f);
    return 1-(1-t)*(1-t)*(1-t);
}
// Composites cached page images; no text, assets or map resampling per frame.
inline void SlidePages(uint32_t* out,const uint32_t* previous,const uint32_t* next,int direction,float fraction){
    if(!out || !previous || !next)return;
    fraction=std::isfinite(fraction)?std::clamp(fraction,0.f,1.f):1.f;
    int shift=int(std::lround(fraction*1240));
    for(int y=0;y<1080;++y){
        auto* row=out+y*1240;const auto* a=previous+y*1240;const auto* b=next+y*1240;
        if(direction>=0){
            std::memcpy(row,a+shift,size_t(1240-shift)*4);
            std::memcpy(row+1240-shift,b,size_t(shift)*4);
        }else{
            std::memcpy(row,b+1240-shift,size_t(shift)*4);
            std::memcpy(row+shift,a,size_t(1240-shift)*4);
        }
    }
}
}

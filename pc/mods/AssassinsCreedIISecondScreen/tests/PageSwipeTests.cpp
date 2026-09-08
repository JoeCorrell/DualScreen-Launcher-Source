#include "PageSwipe.h"
#include <cassert>
#include <vector>
#include <chrono>
#include <cstdio>
using namespace ac2ds;
int main(){
    PageSwipe swipe;
    for(bool map:{false,true})for(int direction:{-1,1}){
        float start=direction>0?.98f:.02f,end=direction>0?.3f:.7f;
        swipe.Down(start,.5f,map,100);assert(swipe.Move(end,.51f));
        assert(swipe.Up(end,.51f,200)==direction && swipe.SuppressTap(201));
        assert(!swipe.SuppressTap(551));
    }
    swipe.Down(.5f,.5f,true,100);assert(!swipe.Move(.1f,.5f));assert(!swipe.Up(.1f,.5f,200));
    swipe.Down(.5f,.03f,true,100);assert(swipe.Up(.2f,.03f,200)==1);
    swipe.Down(.8f,.5f,false,100);assert(!swipe.Move(.8f,.4f));
    assert(!swipe.Move(.2f,.4f));assert(!swipe.Up(.2f,.4f,200)); // Direction already belongs to vertical scrolling.
    swipe.Down(.8f,.5f,false,100);assert(!swipe.Up(.79f,.5f,200));
    swipe.Down(.8f,.5f,false,100);assert(!swipe.Up(.2f,.5f,2000));
    swipe.Down(.8f,.5f,false,100);assert(swipe.Move(.2f,.5f));swipe.Cancel(200);
    assert(!swipe.Up(.2f,.5f,201) && swipe.SuppressTap(202));
    swipe.Down(.8f,.5f,false,203);assert(!swipe.SuppressTap(203)); // A new deliberate gesture is independent.
    swipe.Reset();assert(!swipe.Up(.2f,.5f,220));
    assert(PageSlideFraction(100,99)==0 && PageSlideFraction(100,100)==0 && PageSlideFraction(100,320)==1);
    assert(PageSlideFraction(100,210)>.5f);
    constexpr size_t pixels=1240*1080;
    std::vector<uint32_t> before(pixels),after(pixels),out(pixels+2,0xdeadbeef);
    for(size_t i=0;i<pixels;++i){before[i]=uint32_t(i);after[i]=uint32_t(i)|0x80000000;}
    for(int direction:{-1,1})for(float fraction:{0.f,.25f,.5f,.75f,1.f}){
        SlidePages(out.data()+1,before.data(),after.data(),direction,fraction);
        int shift=int(std::lround(fraction*1240));
        for(int y=0;y<1080;++y)for(int x=0;x<1240;++x){
            bool incoming=direction>0?x>=1240-shift:x<shift;
            int sourceX=direction>0?(incoming?x-(1240-shift):x+shift):(incoming?x+1240-shift:x-shift);
            auto expected=(incoming?after:before)[y*1240+sourceX];assert(out[1+y*1240+x]==expected);
        }
        assert(out.front()==0xdeadbeef && out.back()==0xdeadbeef);
    }
    auto begin=std::chrono::steady_clock::now();
    for(unsigned i=0;i<100;++i)SlidePages(out.data()+1,before.data(),after.data(),1,float(i%50)/50);
    auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
    std::printf("Page swipes: direction lock, map pan isolation, edge/header routes, canceled/pinch gesture suppression, cached slide parity; meanUs=%lld\n",static_cast<long long>(us/100));
}

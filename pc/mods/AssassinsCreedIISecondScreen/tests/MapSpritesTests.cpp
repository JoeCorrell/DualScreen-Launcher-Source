#include "MapSprites.h"
#include <cassert>
#include <cstdio>
using namespace ac2ds;
struct Icons final:ImageSource{
    std::shared_ptr<Image> image=std::make_shared<Image>();
    Icons(){image->width=image->height=2;image->bgra.assign(4,0xffff0000);}
    std::shared_ptr<const Image> Request(uint32_t)override{return image;}
    uint64_t Revision()const override{return 0;}
};
int main(){
    constexpr uint32_t background=0xffdddddd,guard=0x11223344;
    std::vector<uint32_t> storage(size_t(Width)*Height+2,background);storage.front()=storage.back()=guard;
    auto output=storage.data()+1;size_t pixels=storage.size()-2;
    auto map=std::make_shared<Image>();map->width=map->height=1024;
    Snapshot s;s.capabilities=Map;s.map=map;
    Marker m;m.id=1;m.x=m.y=.5f;m.visible=m.discovered=true;m.artworkId=1;s.markers={m};
    Icons icons;MapSprites cache;
    auto reset=[&]{std::fill(output,output+pixels,background);};
    auto count=[&]{return std::count(output,output+pixels,0xffff0000);};
    cache.Draw(output,pixels,s,{},0,0,&icons);assert(count()==36*36);
    reset();s.markers[0].categoryId=276391;cache.Draw(output,pixels,s,{},0,0,&icons);assert(count()==20*20);
    reset();cache.Draw(output,pixels,s,{},276370,0,&icons);assert(count()==0);
    reset();s.markers[0].categoryId=282904;cache.Draw(output,pixels,s,{},0,0,&icons);assert(count()==0);
    s.markers[0].categoryId=276391;s.markers[0].discovered=false;
    cache.Draw(output,pixels,s,{},0,0,&icons);assert(count()==0);s.markers[0].discovered=true;
    // Same asset ID replaced after reload must invalidate its scaled copy.
    auto replacement=std::make_shared<Image>(*icons.image);replacement->bgra.assign(4,0xff00ff00);icons.image=replacement;
    reset();cache.Draw(output,pixels,s,{},0,0,&icons);assert(count()==0);
    assert(std::count(output,output+pixels,0xff00ff00)==400);
    for(float x:{0.f,.5f,1.f})for(float y:{0.f,.5f,1.f})for(float z:{1.f,2.f,8.f}){
        reset();s.markers[0].x=x;s.markers[0].y=y;MapViewport v;v.zoom=z;cache.Draw(output,pixels,s,v,0,0,&icons);
        auto r=MapBounds(map.get());for(int yy=0;yy<Height;++yy)for(int xx=0;xx<Width;++xx)
            if(xx<std::ceil(r.x)||xx>=std::ceil(r.x+r.width)||yy<std::ceil(r.y)||yy>=std::ceil(r.y+r.height))
                assert(output[size_t(yy)*Width+xx]==background);
        assert(storage.front()==guard && storage.back()==guard);
    }
    reset();cache.Draw(output,pixels-1,s,{},0,0,&icons);assert(std::count(output,output+pixels,background)==int(pixels));
    std::puts("Map sprites: native enemy size, discovery/filter exclusion, asset replacement and clipping at all zooms passed");
}

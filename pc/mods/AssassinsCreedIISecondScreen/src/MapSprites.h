#pragma once
#include "MapRaster.h"
#include "MapLegend.h"
#include <map>
namespace ac2ds {
// UI-thread-only, bounded cache. Never decodes archives on a gesture frame;
// ImageSource queues missing art on its existing worker.
class MapSprites {
    struct Sprite {std::weak_ptr<const Image> source;Image scaled;};
    std::map<std::pair<uint32_t,int>,Sprite> sprites_;
    const Image* Get(ImageSource* images,uint32_t id,int size){
        if(!images || !id)return nullptr;
        auto image=images->Request(id);if(!image)return nullptr;
        if(image->width<=0 || image->height<=0 || image->width>4096 || image->height>4096 ||
           image->bgra.size()!=size_t(image->width)*image->height)return nullptr;
        auto key=std::make_pair(id,size);auto found=sprites_.find(key);
        if(found!=sprites_.end() && found->second.source.lock()==image)return &found->second.scaled;
        if(sprites_.size()>=64)sprites_.clear();
        Sprite sprite;sprite.source=image;
        float scale=std::min(float(size)/image->width,float(size)/image->height);
        sprite.scaled.width=std::max(1,int(std::round(image->width*scale)));
        sprite.scaled.height=std::max(1,int(std::round(image->height*scale)));
        auto& s=sprite.scaled;s.bgra.resize(size_t(s.width)*s.height);
        for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x){
            float sx=std::clamp((x+.5f)*image->width/s.width-.5f,0.f,float(image->width-1));
            float sy=std::clamp((y+.5f)*image->height/s.height-.5f,0.f,float(image->height-1));
            int x0=int(sx),y0=int(sy),x1=std::min(x0+1,image->width-1),y1=std::min(y0+1,image->height-1);
            unsigned wx=unsigned((sx-x0)*256),wy=unsigned((sy-y0)*256);
            s.bgra[size_t(y)*s.width+x]=MapLerp(MapLerp(image->bgra[size_t(y0)*image->width+x0],image->bgra[size_t(y0)*image->width+x1],wx),
                MapLerp(image->bgra[size_t(y1)*image->width+x0],image->bgra[size_t(y1)*image->width+x1],wx),wy);
        }
        return &sprites_.insert_or_assign(key,std::move(sprite)).first->second.scaled;
    }
public:
    void Draw(uint32_t* output,size_t pixels,const Snapshot& state,MapViewport view,
            uint32_t filter,uint64_t selected,ImageSource* images){
        if(!output || pixels<size_t(Width)*Height || !(state.capabilities&Map) || !state.map)return;
        view.Clamp();auto r=MapBounds(state.map.get());
        int left=int(std::ceil(r.x)),right=int(std::ceil(r.x+r.width));
        int top=int(std::ceil(r.y)),bottom=int(std::ceil(r.y+r.height));
        for(const auto& marker:state.markers){
            if(!MapMarkerVisible(marker,filter))continue;
            float cx=r.x+((marker.x-view.x)*view.zoom+.5f)*r.width;
            float cy=r.y+((marker.y-view.y)*view.zoom+.5f)*r.height;
            if(cx<r.x || cx>=r.x+r.width || cy<r.y || cy>=r.y+r.height)continue;
            int size=int(MapMarkerSize(marker));auto sprite=Get(images,marker.artworkId,size);
            if(sprite){
                int ox=int(std::round(cx-sprite->width*.5f)),oy=int(std::round(cy-sprite->height*.5f));
                for(int y=std::max(top,oy);y<std::min(bottom,oy+sprite->height);++y)
                    for(int x=std::max(left,ox);x<std::min(right,ox+sprite->width);++x){
                        uint32_t c=sprite->bgra[size_t(y-oy)*sprite->width+x-ox];unsigned alpha=c>>24;
                        auto& p=output[size_t(y)*Width+x];if(alpha)p=alpha==255?c:MapLerp(p,c,alpha+(alpha>>7))|0xff000000;
                    }
            }else{
                float radius=size*7.f/36;uint32_t c=marker.id==selected?0xff89181f:0xff2a2e2f;
                for(int y=std::max(top,int(cy-radius));y<std::min(bottom,int(std::ceil(cy+radius)));++y)
                    for(int x=std::max(left,int(cx-radius));x<std::min(right,int(std::ceil(cx+radius)));++x)
                        if((x+.5f-cx)*(x+.5f-cx)+(y+.5f-cy)*(y+.5f-cy)<=radius*radius)output[size_t(y)*Width+x]=c;
            }
        }
    }
};
}

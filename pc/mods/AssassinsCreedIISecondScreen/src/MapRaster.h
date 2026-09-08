#pragma once
#include "State.h"
#include "Touch.h"
#if defined(__i386__) || defined(__x86_64__)
#include <emmintrin.h>
#endif

namespace ac2ds {
struct MapRect { float x=382,y=96,width=838,height=906; };
inline MapRect MapBounds(const Image* image) {
    MapRect r;
    if(image && image->width>0 && image->height>0) {
        float scale=std::min(r.width/image->width,r.height/image->height);
        float w=scale*image->width,h=scale*image->height;
        r.x+=(r.width-w)/2;r.y+=(r.height-h)/2;r.width=w;r.height=h;
    }
    return r;
}
inline uint32_t MapLerp(uint32_t a,uint32_t b,unsigned weight) {
    unsigned inverse=256-weight;
    return ((((a&0x00ff00ff)*inverse+(b&0x00ff00ff)*weight)>>8)&0x00ff00ff) |
        ((((a>>8)&0x00ff00ff)*inverse+((b>>8)&0x00ff00ff)*weight)&0xff00ff00);
}
// Four pixels at a time on the same SSE2 baseline used by this exact AC II
// executable. Keep the scalar alpha fallback for non-opaque test/custom art.
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
__attribute__((target("sse2")))
#endif
inline void MapVerticalRow(uint32_t* output,const uint32_t* first,const uint32_t* second,
        const uint32_t* backdrop,int count,unsigned weight){
    int x=0;
#if defined(__i386__) || defined(__x86_64__)
    const auto zero=_mm_setzero_si128(),w=_mm_set1_epi16(short(weight)),v=_mm_set1_epi16(short(256-weight));
    const auto alpha=_mm_set1_epi32(int(0xff000000u));
    for(;x+4<=count;x+=4){
        auto a=_mm_loadu_si128(reinterpret_cast<const __m128i*>(first+x));
        auto b=_mm_loadu_si128(reinterpret_cast<const __m128i*>(second+x));
        auto lo=_mm_add_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(a,zero),v),_mm_mullo_epi16(_mm_unpacklo_epi8(b,zero),w));
        auto hi=_mm_add_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(a,zero),v),_mm_mullo_epi16(_mm_unpackhi_epi8(b,zero),w));
        auto color=_mm_packus_epi16(_mm_srli_epi16(lo,8),_mm_srli_epi16(hi,8));
        if(_mm_movemask_epi8(_mm_cmpeq_epi32(_mm_and_si128(color,alpha),alpha))==65535)
            _mm_storeu_si128(reinterpret_cast<__m128i*>(output+x),color);
        else for(int i=0;i<4;++i){
            uint32_t c=MapLerp(first[x+i],second[x+i],weight);unsigned a8=c>>24;
            output[x+i]=a8==255?c:MapLerp(backdrop[x+i],c,a8+(a8>>7))|0xff000000;
        }
    }
#endif
    for(;x<count;++x){uint32_t c=MapLerp(first[x],second[x],weight);unsigned a=c>>24;
        output[x]=a==255?c:MapLerp(backdrop[x],c,a+(a>>7))|0xff000000;}
}
// Bounded bilinear resampling directly into the presentation DIB. Wine's
// GDI+ image scaling crosses a much more expensive path for every gesture.
// Source is the already discovery-masked immutable map, never raw city art.
inline bool RasterMap(uint32_t* destination,size_t pixels,const Image& source,MapViewport view,
        const uint32_t* backdrop=nullptr) {
    if(!destination || pixels<size_t(Width)*Height || source.width<=0 || source.height<=0 ||
        source.width>4096 || source.height>4096 || source.bgra.size()!=size_t(source.width)*source.height) return false;
    view.Clamp();auto r=MapBounds(&source);
    int left=std::max(0,int(std::ceil(r.x))),top=std::max(0,int(std::ceil(r.y)));
    int right=std::min(Width,int(std::ceil(r.x+r.width))),bottom=std::min(Height,int(std::ceil(r.y+r.height)));
    struct Column { int x0,x1;unsigned weight; };
    std::array<Column,Width> columns{};
    for(int x=left;x<right;++x) {
        float sx=std::clamp((view.x+((x+.5f-r.x)/r.width-.5f)/view.zoom)*source.width-.5f,
            0.f,float(source.width-1));
        int x0=int(sx);columns[x]={x0,std::min(x0+1,source.width-1),unsigned((sx-x0)*256)};
    }
    std::array<uint32_t,Width> first{},second{};int firstRow=-1,secondRow=-1;
    auto horizontal=[&](std::array<uint32_t,Width>& output,int row) {
        const auto* input=source.bgra.data()+size_t(row)*source.width;
        for(int x=left;x<right;++x) {const auto& c=columns[x];output[x]=MapLerp(input[c.x0],input[c.x1],c.weight);}
    };
    for(int y=top;y<bottom;++y) {
        float sy=std::clamp((view.y+((y+.5f-r.y)/r.height-.5f)/view.zoom)*source.height-.5f,
            0.f,float(source.height-1));
        int y0=int(sy),y1=std::min(y0+1,source.height-1);unsigned weight=unsigned((sy-y0)*256);
        if(firstRow!=y0) {
            if(secondRow==y0) {first.swap(second);std::swap(firstRow,secondRow);}
            else {horizontal(first,y0);firstRow=y0;}
        }
        if(secondRow!=y1) {horizontal(second,y1);secondRow=y1;}
        auto* output=destination+size_t(y)*Width;
        const auto* base=backdrop?backdrop+size_t(y)*Width:output;
        MapVerticalRow(output+left,first.data()+left,second.data()+left,base+left,right-left,weight);
    }
    return true;
}
}

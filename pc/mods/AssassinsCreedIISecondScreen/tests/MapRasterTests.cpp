#include "MapRaster.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
using namespace ac2ds;
int main() {
    std::mt19937 random(71235);
    for(bool opaque:{false,true})for(unsigned weight=0;weight<=256;++weight){
        std::array<uint32_t,39> a{},b{},base{},fast{},alias{},expected{};
        for(size_t i=0;i<a.size();++i){a[i]=random();b[i]=random();base[i]=random()|0xff000000;
            if(opaque){a[i]|=0xff000000;b[i]|=0xff000000;}
            uint32_t color=MapLerp(a[i],b[i],weight);unsigned alpha=color>>24;
            expected[i]=alpha==255?color:MapLerp(base[i],color,alpha+(alpha>>7))|0xff000000;
        }
        MapVerticalRow(fast.data(),a.data(),b.data(),base.data(),int(a.size()),weight);assert(fast==expected);
        alias=base;MapVerticalRow(alias.data(),a.data(),b.data(),alias.data(),int(a.size()),weight);assert(alias==expected);
    }
    constexpr uint32_t untouched=0xff123456;
    std::vector<uint32_t> output(size_t(Width)*Height,untouched);
    Image image;image.width=4;image.height=3;
    for(unsigned y=0;y<3;++y)for(unsigned x=0;x<4;++x)
        image.bgra.push_back(0xff000000|(x*75)<<16|(y*110)<<8|(x+y)*45);
    for(float zoom:{1.f,2.f,8.f}) {
        MapViewport view;view.zoom=zoom;view.x=.6f;view.y=.4f;view.Clamp();
        std::fill(output.begin(),output.end(),untouched);
        assert(RasterMap(output.data(),output.size(),image,view));auto r=MapBounds(&image);
        for(int y=0;y<Height;++y)for(int x=0;x<Width;++x) {
            auto pixel=output[size_t(y)*Width+x];
            if(x<std::ceil(r.x)||x>=std::ceil(r.x+r.width)||y<std::ceil(r.y)||y>=std::ceil(r.y+r.height)) {
                assert(pixel==untouched);continue;
            }
            // Independent floating-point oracle for pixel-center mapping.
            double sx=std::clamp((view.x+((x+.5-r.x)/r.width-.5)/view.zoom)*image.width-.5,0.,double(image.width-1));
            double sy=std::clamp((view.y+((y+.5-r.y)/r.height-.5)/view.zoom)*image.height-.5,0.,double(image.height-1));
            int x0=int(sx),y0=int(sy),x1=std::min(x0+1,image.width-1),y1=std::min(y0+1,image.height-1);
            for(unsigned shift:{0u,8u,16u}) {
                auto channel=[&](int xx,int yy){return double((image.bgra[yy*image.width+xx]>>shift)&255);};
                double top=channel(x0,y0)*(1-(sx-x0))+channel(x1,y0)*(sx-x0);
                double bottom=channel(x0,y1)*(1-(sx-x0))+channel(x1,y1)*(sx-x0);
                double expected=top*(1-(sy-y0))+bottom*(sy-y0);
                assert(std::abs(double((pixel>>shift)&255)-expected)<2.5);
            }
        }
    }
    auto original=output;
    assert(!RasterMap(nullptr,output.size(),image,{}));
    assert(!RasterMap(output.data(),output.size()-1,image,{}));
    image.width=4097;assert(!RasterMap(output.data(),output.size(),image,{}));assert(output==original);
    image.width=image.height=1;image.bgra={0x00ffffff};
    assert(RasterMap(output.data(),output.size(),image,{}));assert(output==original);
    // Repeated zoom renders use the immutable background, never the prior
    // blended map frame: no accumulating opacity/ghost trails.
    auto backdrop=output;image.bgra={0x80705020};
    assert(RasterMap(output.data(),output.size(),image,{},backdrop.data()));auto once=output;
    assert(RasterMap(output.data(),output.size(),image,{},backdrop.data()));assert(output==once);
    // Realistic raster size, opaque already-masked input, panning and zooming.
    image.width=image.height=1024;image.bgra.assign(1024*1024,0xffdadee0);
    auto start=std::chrono::steady_clock::now();
    for(int i=0;i<60;++i) {
        MapViewport view;view.zoom=1+i*.1f;view.x=.4f+i*.003f;
        assert(RasterMap(output.data(),output.size(),image,view));
    }
    auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"Map bilinear oracle, aspect ratio, bounds, alpha and zoom tests passed; raster meanUs="<<us/60<<"\n";
}

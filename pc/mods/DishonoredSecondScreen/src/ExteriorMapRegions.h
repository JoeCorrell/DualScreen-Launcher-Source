#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace dishonored {
struct ExteriorMapTriangle { float ax,ay,az,bx,by,bz,cx,cy,cz; uint8_t map; };
inline constexpr ExteriorMapTriangle ExteriorMapTriangles[] = {
#include "ExteriorMapRegions.inc"
};
struct ExteriorMapCell { int map,x,y; uint32_t start,count; };
inline constexpr ExteriorMapCell ExteriorMapCells[] = {
#include "ExteriorMapCells.inc"
};
inline constexpr uint32_t ExteriorMapIndices[] = {
#include "ExteriorMapIndices.inc"
};
// Eligibility for schematic outdoor artwork, not collision or discovery data.
inline bool ExteriorMapContains(uint8_t map,double x,double y,double z) noexcept {
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return false;
    if(map==6 && (x < -11500 || x > 3200 || y < -29500 || y > -16000))return false;
    // The moving central span is represented by its cartographic corridor
    // between the native tower inner edges. This does not assert deck state.
    if(map==2 && x>=17106 && x<=21216 && y>=-199 && y<=1442 && z>=380 && z<=5800)return true;
    if(std::abs(x)>100000 || std::abs(y)>100000)return false;
    const int gx=int(std::floor(x/2048)),gy=int(std::floor(y/2048));
    // Sorted cells permit logarithmic lookup; only nearby triangles are read.
    std::size_t lo=0,hi=sizeof(ExteriorMapCells)/sizeof(ExteriorMapCells[0]);
    while(lo<hi) {
        const std::size_t mid=lo+(hi-lo)/2;const auto& c=ExteriorMapCells[mid];
        if(c.map<map || (c.map==map && (c.x<gx || (c.x==gx && c.y<gy))))lo=mid+1;
        else hi=mid;
    }
    if(lo>=sizeof(ExteriorMapCells)/sizeof(ExteriorMapCells[0]))return false;
    const auto& cell=ExteriorMapCells[lo];
    if(cell.map!=map || cell.x!=gx || cell.y!=gy)return false;
    const double height=map==2?3000:2000;
    for(uint32_t i=0;i<cell.count;++i) {
        const auto& t=ExteriorMapTriangles[ExteriorMapIndices[cell.start+i]];
        const double bx=double(t.bx)-t.ax,by=double(t.by)-t.ay;
        const double cx=double(t.cx)-t.ax,cy=double(t.cy)-t.ay;
        const double det=bx*cy-by*cx;
        if(std::abs(det)<1)continue;
        const double dx=x-t.ax,dy=y-t.ay;
        const double u=(dx*cy-dy*cx)/det,v=(bx*dy-by*dx)/det;
        if(u < -0.00001 || v < -0.00001 || u+v > 1.00001)continue;
        const double gap=z-(t.az+u*(double(t.bz)-t.az)+v*(double(t.cz)-t.az));
        if(gap>=-0.001 && gap<=height)return true;
    }
    return false;
}
}

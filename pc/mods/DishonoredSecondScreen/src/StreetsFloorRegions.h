#pragma once
#include <cmath>
#include <cstdint>
namespace dishonored {
struct StreetsFloorTriangle { float ax,ay,az,bx,by,bz,cx,cy,cz; uint8_t floor; };
inline constexpr StreetsFloorTriangle StreetsFloorTriangles[] = {
#include "StreetsFloorRegions.inc"
};
inline int StreetsFloor(double x,double y,double z) noexcept {
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return -1;
    int result=-1;double nearest=2001;
    for(const auto& t:StreetsFloorTriangles) {
        const double bx=double(t.bx)-t.ax,by=double(t.by)-t.ay;
        const double cx=double(t.cx)-t.ax,cy=double(t.cy)-t.ay;
        const double determinant=bx*cy-by*cx;
        if(std::abs(determinant)<1)continue;
        const double dx=x-t.ax,dy=y-t.ay;
        const double u=(dx*cy-dy*cx)/determinant,v=(bx*dy-by*dx)/determinant;
        if(u < -0.00001 || v < -0.00001 || u+v > 1.00001)continue;
        const double floorZ=t.az+u*(double(t.bz)-t.az)+v*(double(t.cz)-t.az);
        const double gap=z-floorZ;
        // This outdoor drawing has no separated storey columns. Permit known
        // rooftops above the verified street footprint; reject underground gaps.
        if(gap>=-0.001 && gap<=2000 && gap<nearest) { nearest=gap;result=t.floor; }
    }
    return result;
}
}

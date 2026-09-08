#pragma once
#include <cmath>
#include <cstdint>
namespace dishonored {
struct OverseerFloorTriangle { float ax,ay,az,bx,by,bz,cx,cy,cz; uint8_t floor; };
inline constexpr OverseerFloorTriangle OverseerFloorTriangles[] = {
#include "OverseerFloorRegions.inc"
};
inline int OverseerFloor(double x,double y,double z) noexcept {
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return -1;
    int result=-1;double nearest=500;
    for(const auto& t:OverseerFloorTriangles) {
        const double bx=double(t.bx)-t.ax,by=double(t.by)-t.ay;
        const double cx=double(t.cx)-t.ax,cy=double(t.cy)-t.ay;
        const double determinant=bx*cy-by*cx;
        if(std::abs(determinant)<1)continue;
        const double dx=x-t.ax,dy=y-t.ay;
        const double u=(dx*cy-dy*cx)/determinant,v=(bx*dy-by*dx)/determinant;
        if(u < -0.00001 || v < -0.00001 || u+v > 1.00001)continue;
        const double floorZ=t.az+u*(double(t.bz)-t.az)+v*(double(t.cz)-t.az);
        const double gap=z-floorZ;
        // Half the gap between the measured lower-floor maximum (352) and
        // upper-floor minimum (1350). Ambiguous stairs/airborne gaps stay unknown.
        if(gap>=-0.001 && gap<=499 && gap<nearest) { nearest=gap;result=t.floor; }
    }
    return result;
}
}

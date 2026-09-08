#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include "OverseerFloorRegions.h"
#include "BrothelFloorRegions.h"
#include "TowerFloorRegions.h"
#include "StreetsFloorRegions.h"
#include "ExteriorMapRegions.h"

namespace dishonored {
// Cartographic calibration, not an engine-provided transform. All three
// Kingsparrow drawings share 800x500 artwork; their printed board symbols
// supply three correspondences with serialized level actors. Independently
// checked against native terrain/dock/wall vertices and lighthouse geometry.
// See MAP-CALIBRATION.md. Other drawings require their own validated regions.
struct MapProjection { float x{}, y{}, facing{}; bool positionKnown{}, facingKnown{}; };
inline bool HasMapCalibration(uint8_t map) noexcept { return map >= 1 && map <= 13; }
inline MapProjection ProjectAcquiredMap(uint8_t map, double x, double y, double yawRadians,
    bool yawKnown, double z = std::numeric_limits<double>::quiet_NaN()) noexcept {
    MapProjection result{};
    if (!HasMapCalibration(map) || !std::isfinite(x) || !std::isfinite(y)) return result;
    double xx = 0.00023504629226817273, xy = 0.023267452631206546;
    double yx = -0.0203575676724151, yy = 0.0003859214797155912;
    double offsetX=558.2657854412575,offsetY=204.51588015451213;
    double extraY{},directionYx{},directionYy{};
    if(map==1 || map==2 || map==3 || map==6) {
        if(!ExteriorMapContains(map,x,y,z))return result;
        if(map==1) {
            xx=-.022851647109755614;xy=-.01524812224475952;
            yx=-.010726041161025898;yy=.0016280463569363478;
            offsetX=217.46247609745777;offsetY=169.1029540798954;
            if(y>7700) {
                xy-=86.0/6028;offsetX+=7700*86.0/6028;
                yy+=47.0/6028;offsetY-=7700*47.0/6028;
            } else if(y < -4270) {
                xy+=12.0/5730;offsetX+=4270*12.0/5730;
                yy+=28.0/5730;offsetY+=4270*28.0/5730;
            }
        } else if(map==2) {
            xx=.009533003063224828;xy=.0023258193984313402;
            yx=-.004298912868430227;yy=.0035872254159847925;
            offsetX=254.60242070496594;offsetY=329.53644081137355;
        } else if(map==3) {
            xx=.02049470111001804;xy=-.026224363919331046;
            yx=.012621106475667412;yy=.01834330711382919;
            offsetX=-81.38529551298585;offsetY=-457.89961366866606;
        } else {
            xx=.04536478784323526;xy=.001043085490113531;
            yx=-.009797923731013533;yy=.010460134532461427;
            offsetX=540.6306920608376;offsetY=536.4792279666782;
            if(x < -5500) {
                xx-=.03;offsetX-=5500*.03;
                yx+=.042;offsetY+=5500*.042;
            }
        }
    } else if(map==9) {
        if(StreetsFloor(x,y,z)<0)return result;
        xx=0.018186814084506304;xy=-0.031100503196157512;
        yx=0.00830807096367354;yy=0.006133508362493095;
        offsetX=417.9236256143123;offsetY=287.9132351344799;
        // The drawing compresses the bridge and stretches the upper street.
        // These are continuous cartographic corrections, not engine geometry.
        if(y < -5031) {
            xy+=0.0151025266;offsetX+=5031*0.0151025266;
            yy-=0.00518861649;offsetY-=5031*0.00518861649;
        }
        const double rear=std::clamp((-1451.0654296875-x)/4548.9345703125,0.0,1.0);
        const double strip=std::clamp((-3500-y)/1000,0.0,1.0);
        extraY=-77.20712878*rear*strip;
        // Local Jacobian keeps facing aligned with the same warped drawing.
        if(x>-6000 && x<-1451.0654296875)directionYx=77.20712878*strip/4548.9345703125;
        if(y>-4500 && y<-3500)directionYy=77.20712878*rear/1000;
    } else if(map==4) {
        const int floor=BrothelFloor(x,y,z);
        if(floor<0)return result;
        if(floor==0) {
            xx=-0.039;xy=0;yx=0;yy=-0.037;
            offsetX=1579.85;offsetY=705.9;
        } else if(floor==1) {
            xx=-0.03234988851287561;xy=-0.001662672707086507;
            yx=9.601864914678965e-05;yy=-0.03279976961265231;
            offsetX=1588.889758534625;offsetY=642.6855391393325;
        } else {
            xx=-0.03386274607509839;xy=0.0010011303338551933;
            yx=-0.000787826349679499;yy=-0.037201160231865205;
            offsetX=1839.4216547071355;offsetY=737.3369779493324;
        }
    } else if(map==10 || map==13) {
        const int floor=TowerFloor(x,y,z);
        if(floor<0)return result;
        xx=0;yy=0;
        if(floor==0) {
            xy=0.033;yx=-0.033;offsetX=-68.691;offsetY=5.552;
        } else if(floor==1) {
            xy=0.032;yx=-0.0328;offsetX=121.68;offsetY=6.08;
        } else {
            xy=0.03141;yx=-0.0313;offsetX=364.05;offsetY=10.45;
        }
    } else if(map==7 || map==8) {
        const int floor=OverseerFloor(x,y,z);
        if(floor<0)return result;
        if(floor==0) {
            xx=0.06062014493223537;xy=0;yx=0;yy=0.05034961596535186;
            offsetX=396.4038575839634;offsetY=1696.9920242897563;
        } else {
            xx=0.058941826147182366;xy=0.00011877446074998966;
            yx=-7.59650660565086e-05;yy=0.037697664030542394;
            offsetX=511.04153819525254;offsetY=1139.2561326053658;
        }
    }
    const double px = xx*x + xy*y + offsetX;
    const double py = yx*x + yy*y + offsetY + extraY;
    // Do not clamp an unrelated/out-of-area position to a plausible edge marker.
    if (!std::isfinite(px) || !std::isfinite(py) || px < 0 || px > 800 || py < 0 || py > 500) return result;
    result.x = float(px/800); result.y = float(py/500); result.positionKnown = true;
    if (yawKnown && std::isfinite(yawRadians)) {
        const double dx = std::cos(yawRadians), dy = std::sin(yawRadians);
        // Transform the direction through the artwork axes before atan2.
        // Use pixel units: normalized X/Y have different aspect ratios.
        result.facing = float(std::atan2((yx+directionYx)*dx + (yy+directionYy)*dy, xx*dx + xy*dy));
        result.facingKnown = std::isfinite(result.facing);
    }
    return result;
}
}

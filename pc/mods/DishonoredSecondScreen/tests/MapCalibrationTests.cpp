#include "../src/MapCalibration.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <initializer_list>
int main() {
    // Native serialized board positions, independently identified printed centers.
    const double anchors[][4] = {
        {-4459.7685546875, 5685.30078125, 689.5, 297.5},
        {-4571.708984375, -5402.01904296875, 431.5, 295.5},
        {377, -3475, 477.5, 195.5}};
    for (uint8_t map : {5,11,12}) for (const auto& a : anchors) {
        auto p = dishonored::ProjectAcquiredMap(map,a[0],a[1],0,true);
        assert(p.positionKnown && p.facingKnown);
        assert(std::abs(p.x*800-a[2])<0.01 && std::abs(p.y*500-a[3])<0.01);
        assert(p.facing < -1.5f && p.facing > -1.6f); // World +X points up on this drawing.
    }
    constexpr double pi=3.14159265358979323846;
    auto north=dishonored::ProjectAcquiredMap(5,377,-3475,pi/2,true);
    assert(north.facing>0 && north.facing<0.02); // World +Y points right.
    auto wrapped=dishonored::ProjectAcquiredMap(5,377,-3475,pi/2+2*pi,true);
    assert(std::abs(wrapped.facing-north.facing)<0.0001);
    auto unknown=dishonored::ProjectAcquiredMap(5,377,-3475,0,false);
    assert(unknown.positionKnown && !unknown.facingKnown);
    for (uint8_t map : {0,1,2,3,4,6,7,8,9,10,13,255})
        assert(!dishonored::ProjectAcquiredMap(map,377,-3475,0,true).positionKnown);
    const double nan=std::numeric_limits<double>::quiet_NaN();
    assert(!dishonored::ProjectAcquiredMap(5,nan,0,0,true).positionKnown);
    assert(!dishonored::ProjectAcquiredMap(5,0,std::numeric_limits<double>::infinity(),0,true).positionKnown);
    assert(!dishonored::ProjectAcquiredMap(5,1e30,1e30,0,true).positionKnown);
    assert(!dishonored::ProjectAcquiredMap(5,377,-3475,nan,true).facingKnown);
    for(uint8_t map : {7,8}) {
        const auto right=dishonored::ProjectAcquiredMap(map,-291,-28537,0,true,1553);
        const auto left=dishonored::ProjectAcquiredMap(map,-4261,-28545,0,true,1551);
        assert(right.positionKnown && left.positionKnown);
        assert(std::abs(right.x*800-490.5)<0.01 && std::abs(right.y*500-63.5)<0.01);
        assert(std::abs(left.x*800-256.5)<0.01 && std::abs(left.y*500-63.5)<0.01);
        const auto ground=dishonored::ProjectAcquiredMap(map,-1831.2416178385417,-27660.254557291668,0,true,350);
        assert(ground.positionKnown && ground.x*800>280 && ground.x*800<290 && ground.y*500>300 && ground.y*500<310);
        assert(!dishonored::ProjectAcquiredMap(map,-1831.2416178385417,-27660.254557291668,0,true,850).positionKnown);
        assert(!dishonored::ProjectAcquiredMap(map,-10000,-20000,0,true,1500).positionKnown);
    }
    assert(dishonored::OverseerFloor(-291,-28537,1553)==1);
    assert(dishonored::OverseerFloor(-1831.2416178385417,-27660.254557291668,350)==0);
    assert(dishonored::OverseerFloor(0,0,nan)==-1);
    const double catFloors[][4] = {
        {35783.83454386393,15366.68354288737,3400,0},
        {37987.941731770836,15038.575398763021,4420,1},
        {37965.941731770836,15042.575398763021,5350,2}};
    for(const auto& point:catFloors) {
        assert(dishonored::BrothelFloor(point[0],point[1],point[2])==int(point[3]));
        const auto p=dishonored::ProjectAcquiredMap(4,point[0],point[1],0,true,point[2]);
        assert(p.positionKnown && p.facingKnown);
        assert(p.x*800>60+point[3]*225 && p.x*800<260+point[3]*230);
        assert(!dishonored::ProjectAcquiredMap(4,point[0],point[1],0,true,point[2]+250).positionKnown);
    }
    const auto catBoard=dishonored::ProjectAcquiredMap(4,36389,8847,0,true,4550);
    assert(catBoard.positionKnown && std::abs(catBoard.x*800-397)<0.01 && std::abs(catBoard.y*500-356)<0.01);
    assert(!dishonored::ProjectAcquiredMap(4,36389,8847,0,true,nan).positionKnown);
    assert(!dishonored::ProjectAcquiredMap(4,42361,8190,0,true,3242).positionKnown);
    puts("PASS: Golden Cat three floor columns, native board anchor, unknown height, exterior and ambiguous elevation rejection");
    for(uint8_t map : {10,13}) {
        const double towerFloors[][4]={{-6766.666666666667,8233.333333333334,4275.5,0},
            {-4204.332682291667,4406.666015625,4900,1},{-2756.6666666666665,5060.000651041667,6000,2}};
        for(const auto& point:towerFloors) {
            assert(dishonored::TowerFloor(point[0],point[1],point[2])==int(point[3]));
            const auto p=dishonored::ProjectAcquiredMap(map,point[0],point[1],0,true,point[2]);
            assert(p.positionKnown && p.facingKnown && std::abs(p.facing+pi/2)<0.0001);
            assert(p.x*800>90+point[3]*155 && p.x*800<245+point[3]*210);
            assert(!dishonored::ProjectAcquiredMap(map,point[0],point[1],0,true,point[2]+250).positionKnown);
        }
        const auto board=dishonored::ProjectAcquiredMap(map,-12256,6627,0,true,4280);
        assert(board.positionKnown && std::abs(board.x*800-150)<0.01 && std::abs(board.y*500-410)<0.01);
        assert(!dishonored::ProjectAcquiredMap(map,-12256,6627,0,true,nan).positionKnown);
    }
    puts("PASS: Dunwall Tower board, separate three-floor geometry, shared drawings, heading and vertical gap rejection");
    const double streetsAnchors[][5]={{-1451.0654296875,-5031,2728.1455078125,548,245},
        {3343.483154296875,9669.6484375,3533.255126953125,178,375},
        {-5624.34912109375,-1008.516357421875,2449.0185546875,347,235}};
    for(const auto& a:streetsAnchors) {
        const auto p=dishonored::ProjectAcquiredMap(9,a[0],a[1],0,true,a[2]);
        assert(p.positionKnown && p.facingKnown);
        assert(std::abs(p.x*800-a[3])<0.02 && std::abs(p.y*500-a[4])<0.02);
    }
    assert(!dishonored::ProjectAcquiredMap(9,12755,-12698,0,true,1852).positionKnown);
    assert(!dishonored::ProjectAcquiredMap(9,-1451,-5031,0,true,nan).positionKnown);
    assert(!dishonored::ProjectAcquiredMap(9,-1451,-5031,0,true,-10000).positionKnown);
    const double streetDirections[][3]={{426,-8000,3100},{-4000,-5000,3300},{400,0,3200}};
    for(const auto& a:streetDirections) {
        const double angle=0.7,step=20;
        const auto p=dishonored::ProjectAcquiredMap(9,a[0],a[1],angle,true,a[2]);
        const auto q=dishonored::ProjectAcquiredMap(9,a[0]+step*std::cos(angle),a[1]+step*std::sin(angle),0,false,a[2]);
        assert(p.positionKnown && q.positionKnown && p.facingKnown);
        const double actual=std::atan2((q.y-p.y)*500,(q.x-p.x)*800);
        assert(std::abs(actual-p.facing)<0.002);
    }
    const auto before=dishonored::ProjectAcquiredMap(9,426,-5031.01,0,false,3100);
    const auto after=dishonored::ProjectAcquiredMap(9,426,-5030.99,0,false,3100);
    assert(before.positionKnown && after.positionKnown && std::abs(before.x-after.x)<0.00001 && std::abs(before.y-after.y)<0.00001);
    puts("PASS: Distillery District native board/transition anchors and unmapped dock/invalid-height rejection");
    puts("PASS: three board anchors, shared-sheet projection, transformed heading, unknown data and out-of-chart rejection");
    puts("PASS: Overseer board anchors, separate native floor membership, ground-floor region, ambiguous height and out-of-floor rejection");
    for(uint8_t map : {1,2,3,6}) {
        assert(dishonored::HasMapCalibration(map));
        assert(!dishonored::ProjectAcquiredMap(map,0,0,0,true,nan).positionKnown);
        assert(!dishonored::ProjectAcquiredMap(map,1e20,1e20,0,true,100).positionKnown);
        unsigned checked=0;
        for(const auto& t:dishonored::ExteriorMapTriangles) {
            if(t.map!=map)continue;
            const double x=(double(t.ax)+t.bx+t.cx)/3,y=(double(t.ay)+t.by+t.cy)/3;
            const double z=(double(t.az)+t.bz+t.cz)/3+100;
            const auto p=dishonored::ProjectAcquiredMap(map,x,y,.7,true,z);
            if(!p.positionKnown)continue;
            assert(p.facingKnown && p.x>=0 && p.x<=1 && p.y>=0 && p.y<=1);
            const auto q=dishonored::ProjectAcquiredMap(map,x+20*std::cos(.7),y+20*std::sin(.7),0,false,z);
            // Avoid crossing a cartographic derivative seam during this check.
            if(q.positionKnown && std::abs(y-7700)>25 && std::abs(y+4270)>25 && std::abs(x+5500)>25) {
                const double heading=std::atan2((q.y-p.y)*500,(q.x-p.x)*800);
                assert(std::abs(heading-p.facing)<.002);
                ++checked;
            }
        }
        assert(checked>20);
        printf("PASS: exterior map %u indexed geometry and %u heading samples\n",map,checked);
    }
    const double exteriorBoards[][6]={{2,9156.6162109375,-3393.415283203125,519,334,278},
        {3,42361.59375,8190.96728515625,3242.38916015625,572,227},
        {6,2625.33984375,-16037.685546875,-365.8197021484375,643,343}};
    for(const auto& a:exteriorBoards) {
        const auto p=dishonored::ProjectAcquiredMap(uint8_t(a[0]),a[1],a[2],0,true,a[3]);
        assert(p.positionKnown && std::abs(p.x*800-a[4])<.02 && std::abs(p.y*500-a[5])<.02);
    }
    assert(dishonored::ProjectAcquiredMap(2,19000,600,0,true,1200).positionKnown);
    assert(!dishonored::ProjectAcquiredMap(2,19000,600,0,true,-10000).positionKnown);
    assert(!dishonored::ProjectAcquiredMap(6,-12000,-22000,0,true,1000).positionKnown);
    puts("PASS: remaining exterior board anchors, bridge corridor, unknown and out-of-area rejection");

}

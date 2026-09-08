#include "CharacterPreviewCamera.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace ac2ds::preview;
Constants Pack(const Matrix& m){Constants c{};for(unsigned r=0;r<4;++r)for(unsigned col=0;col<4;++col)c[col*4+r]=float(m[r*4+col]);return c;}
int main(){
    auto old=Camera({12,-8,3},{10,0,1},1.7);auto next=Camera({10,4,1.1},{10,0,1},278./351);
    assert(old && next);auto inv=Inverse(*old);assert(inv);auto identity=Multiply(*old,*inv);
    for(unsigned i=0;i<16;++i)assert(std::abs(identity[i]-(i%5==0?1:0))<1e-9);
    Matrix world{1,0,0,0,0,1,0,0,0,0,1,0,10,0,0,1};
    auto converted=Reframe(Pack(Multiply(world,*old)),Pack(*old),*next);assert(converted);
    auto expected=Multiply(world,*next),actual=FromConstants(*converted);
    for(unsigned i=0;i<16;++i)assert(std::abs(expected[i]-actual[i])<1e-3);
    // Camera aim point projects onto the center with valid D3D9 depth.
    std::array<double,4> point{10,0,1,1},clip{};
    for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)clip[c]+=point[r]*next->at(r*4+c);
    assert(std::abs(clip[0])<1e-8 && std::abs(clip[1])<1e-8 && clip[3]>0 && clip[2]>0 && clip[2]<clip[3]);
    assert(!Inverse(Matrix{}));assert(!Camera({0,0,0},{0,0,0},1));assert(!Camera({0,0,0},{0,0,1},1));
    assert(!Camera({0,1,0},{0,0,0},0));assert(!Camera({0,1,0},{0,0,0},1,std::numeric_limits<double>::infinity()));
    auto broken=Pack(*old);broken[0]=std::numeric_limits<float>::quiet_NaN();assert(!Reframe(broken,Pack(*old),*next));
    assert(!Reframe(Pack(*old),Constants{},*next));
    std::puts("Preview camera: transposed register convention, camera replacement, framing, singular and nonfinite guards passed");
}

#include "../../DeadIslandSecondScreen/src/Canvas.h"
#include <cassert>
#include <cstdio>
namespace {
HDC observed{};unsigned presentations{};bool failScale{};
BOOL copy(HDC target,int x,int y,int w,int h,HDC source,int sx,int sy,DWORD operation) {
    if(target==observed) ++presentations;
    return BitBlt(target,x,y,w,h,source,sx,sy,operation);
}
BOOL scale(HDC target,int x,int y,int w,int h,HDC source,int sx,int sy,int sw,int sh,DWORD operation) {
    assert(target!=observed); // Scaling must finish offscreen before presentation.
    if(failScale) return FALSE;
    return StretchBlt(target,x,y,w,h,source,sx,sy,sw,sh,operation);
}
}
#define BitBlt copy
#define StretchBlt scale
#include "../src/PanelPresentation.h"
#undef BitBlt
#undef StretchBlt
int main() {
    dis::Canvas source,target,staging;
    assert(source.Create(1240,1080) && target.Create(1240,1080));
    source.Clear(dis::Color(120,70,20));observed=target.dc();
    assert(dishonored::PresentPanelFrame(observed,source,staging,{0,0,1240,1080},{0,0,1240,1080}));
    assert(presentations==1 && !staging.ready());
    GdiFlush();assert(GetPixel(observed,620,540)==RGB(120,70,20));
    assert(dishonored::PresentPanelFrame(observed,source,staging,{0,0,1240,1080},{310,270,930,810}));
    assert(presentations==2 && staging.ready());
    GdiFlush();assert(GetPixel(observed,20,20)==RGB(0,0,0));assert(GetPixel(observed,620,540)==RGB(120,70,20));
    source.Clear(dis::Color(200,0,0));failScale=true;
    assert(!dishonored::PresentPanelFrame(observed,source,staging,{0,0,1240,1080},{310,270,930,810}));
    assert(presentations==2 && GetPixel(observed,620,540)==RGB(120,70,20));
    assert(!dishonored::PresentPanelFrame(observed,source,staging,{0,0,1240,1080},{-1,0,1240,1080}));
    assert(presentations==2);
    puts("PASS: one-copy native/scaled presentation, aspect-fit bars, failed scale retains prior frame and invalid bounds rejected");
}

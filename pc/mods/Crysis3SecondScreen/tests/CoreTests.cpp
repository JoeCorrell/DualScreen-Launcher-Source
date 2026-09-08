#include "../src/Layout.h"
#include "../../DishonoredSecondScreen/src/Touch.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <memory>

int main() {
    using namespace c3;
    auto s=std::make_unique<Snapshot>();
    assert(!Current(*s,1000));
    s->adapterVerified=true; s->generation=1; s->sequence=1; s->phase=Phase::Gameplay; s->sampledAt=1000;
    assert(Current(*s,1000)); assert(Current(*s,1750)); assert(!Current(*s,1751)); assert(!Current(*s,999));
    Capability cap{8,Action::ToggleArmor,true,true};
    assert(CanAct(*s,cap,1000)); cap.verified=false; assert(!CanAct(*s,cap,1000)); cap.verified=true;
    Command command{1,1,1,1000,cap}; assert(!Eligible(*s,command,1000));
    s->toggleArmor=cap; assert(Eligible(*s,command,1000));
    command.generation=2; assert(!Eligible(*s,command,1000)); command.generation=1;
    s->phase=Phase::Paused; assert(Current(*s,1000)); assert(!CanAct(*s,cap,1000));
    for (Phase phase:{Phase::Loading,Phase::Menu,Phase::Cutscene,Phase::Dead,Phase::Stopped}) {
        s->phase=phase; assert(!Current(*s,1000)); assert(!Eligible(*s,command,1000));
    }
    assert(Ratio({}, {100,true})<0); assert(Ratio({0,true},{100,true})==0);
    assert(Ratio({100,true},{0,true})<0); assert(Ratio({std::numeric_limits<float>::quiet_NaN(),true},{100,true})<0);
    assert(Ratio({200,true},{100,true})==1);
    auto fit=Fit::Into(1280,1080); float x=20,y=0; assert(fit.Point(x,y)&&x==0&&y==0);
    x=19; y=40; assert(!fit.Point(x,y)); x=1260;y=40;assert(!fit.Point(x,y));
    assert(Content.x==24 && Content.w==1192 && Content.x+Content.w<=Width);
    NavigationState cycle;
    static_assert(int(c3::Page::Count)==5);
    for (int i=0;i<int(c3::Page::Count);++i) { cycle.Down(800,150); cycle.Up(500,150); assert(int(cycle.page)==(i+1)%int(c3::Page::Count)); }
    cycle.Down(500,150); cycle.Up(800,150); assert(cycle.page==Page::Status);
    NavigationState n; n.Down(800,150); n.Up(500,150); assert(n.page==Page::Weapons);
    Snapshot disconnected{}; n.Synchronize(disconnected,1000); n.Down(800,150); n.Synchronize(disconnected,1001);
    n.Up(500,150); assert(n.page==Page::Suit); n.page=Page::Weapons;
    n.Down(500,150); n.Cancel(); n.Up(900,150); assert(n.page==Page::Weapons);
    n.Down(500,300); n.Move(600,350); n.Move(500,300); assert(!n.Up(500,300));
    n.Down(500,300); n.Move(510,310); assert(n.Up(510,310));
    n.Down(500,300); n.Move(std::numeric_limits<float>::quiet_NaN(),300); assert(!n.Up(500,300));
    n.Down(std::numeric_limits<float>::infinity(),300); assert(!n.Up(500,300));
    n.page=Page::Map; n.mapReady=true; n.Down(700,400); n.Move(300,400); n.Up(300,400); assert(n.page==Page::Map);
    MapView m; m.Pinch(700,400,2); assert(m.zoom==2);
    m.Pan(100000,-100000); assert(m.centerX>=0.25f && m.centerY<=0.75f);
    float previous=m.zoom; m.Pinch(700,400,std::numeric_limits<float>::infinity()); assert(m.zoom==previous);
    s->generation=3; n.Synchronize(*s,1000); assert(n.map.zoom==1);
    auto mailbox=std::make_unique<Mailbox>(); s->phase=Phase::Gameplay;
    assert(mailbox->Publish(*s)); assert(!mailbox->Publish(*s));
    auto copy=std::make_unique<Snapshot>(); assert(mailbox->Read(*copy)); assert(copy->generation==3);
    s->generation=2; ++s->sequence; assert(!mailbox->Publish(*s));
    dishonored::Touch touch{};
    for (const char* packet:{"down 0.5 0.2","tap 0 1 80","pinch 0.5 0.5 1.2","cancel","page next"}) assert(dishonored::ParseTouch(packet,touch));
    for (const char* packet:{"tap nan 0","down -1 0","move 0 2","tap 0 0 junk","pinch 0 0 inf","page other"}) assert(!dishonored::ParseTouch(packet,touch));
    std::cout<<"PASS: freshness, lifecycle, actions, invalidation, ratios, fit, navigation, map gestures, mailbox, touch parsing\n";
}

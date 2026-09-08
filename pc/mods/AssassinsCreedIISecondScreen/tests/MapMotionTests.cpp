#include "MapMotion.h"
#include <cassert>
#include <iostream>
using namespace ac2ds;
int main() {
    Snapshot s;s.phase=Phase::Playing;s.capabilities=Map|Player;s.map=std::make_shared<Image>();
    s.generation=1;s.playerIdentity=2;s.worldIdentity=3;s.sampledAt=1000;
    s.mapX=.4f;s.mapY=.4f;s.heading=3.1f;MapMotion motion;
    assert(motion.Update(s,1000).x==s.mapX);
    s.mapX=.42f;s.mapY=.44f;s.heading=-3.1f;s.sampledAt=1250;
    assert(motion.Update(s,1250).x==.4f);
    auto half=motion.Update(s,1375);
    assert(std::abs(*half.x-.41f)<.00001f && std::abs(*half.y-.42f)<.00001f);
    assert(std::abs(*half.heading-3.14159265f)<.00001f);
    assert(motion.Update(s,1500).x==s.mapX);
    s.mapX=.9f;s.sampledAt=1750;assert(motion.Update(s,1750).x==.9f); // teleport
    s.generation++;s.mapX=.91f;s.sampledAt=2000;assert(motion.Update(s,2000).x==.91f);
    s.mapX=.92f;s.heading.reset();s.sampledAt=2250;assert(!motion.Update(s,2250).heading);
    s.capabilities=Player;assert(!motion.Update(s,2270).x); // failed discovery/map guard
    s.capabilities=Player|Map;s.sampledAt=2500;assert(motion.Update(s,2500).x==s.mapX);
    s.mapX=.93f;s.worldIdentity++;s.sampledAt=2750;assert(motion.Update(s,2750).x==s.mapX);
    s.mapX=.94f;s.playerIdentity++;s.sampledAt=3000;assert(motion.Update(s,3000).x==s.mapX);
    std::cout<<"Observed map pose interpolation, angle wrapping, teleport/load and invalid-state guards passed\n";
}

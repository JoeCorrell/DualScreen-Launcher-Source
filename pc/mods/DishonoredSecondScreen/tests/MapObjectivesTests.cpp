#include "../src/MapObjectives.h"
#include "../src/UiVisualState.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <memory>
int main() {
    using namespace dishonored;
    auto state=std::make_unique<bridge::Snapshot>(); auto& s=*state;
    s.lifecycle=bridge::Lifecycle::Live;s.generation=1;s.sampledAt=2000;
    s.text[1]=u'A';s.text[2]=0;s.textUsed=3;
    s.objectiveTargets.domain={bridge::Availability::Available,1,2,2};
    auto& first=s.objectiveTargets.entries[0];
    first.task=123;first.observedAt=2000;first.label.title={1,1};
    first.worldX={377,true};first.worldY={-3475,true};
    auto& second=s.objectiveTargets.entries[1];second=first;second.task=456;second.optional=true;
    bridge::Map map{};map.key=5;map.acquired={true,true};map.label.artwork.id=2005;
    ProjectMapObjectives(s,map,2000);
    assert(map.markers.domain.stored==2);
    assert(map.markers.entries[0].kind==bridge::MarkerKind::PrimaryObjective);
    assert(map.markers.entries[1].kind==bridge::MarkerKind::OptionalObjective);
    assert(map.markers.entries[0].key!=map.markers.entries[1].key);
    assert(std::abs(map.markers.entries[0].x*800-477.5)<0.01);
    ProjectMapObjectives(s,map,2751);assert(!map.markers.domain.stored);
    ProjectMapObjectives(s,map,1999);assert(!map.markers.domain.stored);
    second.worldX.known=false;
    ProjectMapObjectives(s,map,2000);assert(map.markers.domain.stored==1);
    first.worldX.value=std::numeric_limits<float>::quiet_NaN();
    ProjectMapObjectives(s,map,2000);assert(!map.markers.domain.stored);
    first.worldX={377,true};
    map.acquired.value=false;ProjectMapObjectives(s,map,2000);assert(!map.markers.domain.stored);
    map.acquired.value=true;map.label.artwork.id=2007;
    ProjectMapObjectives(s,map,2000);assert(!map.markers.domain.stored);
    map.key=7;ProjectMapObjectives(s,map,2000);assert(!map.markers.domain.stored);
    map.key=11;map.label.artwork.id=2011;ProjectMapObjectives(s,map,2000);assert(map.markers.domain.stored==1);
    s.lifecycle=bridge::Lifecycle::NoPlayer;ProjectMapObjectives(s,map,2000);assert(!map.markers.domain.stored);
    s.lifecycle=bridge::Lifecycle::Live;
    s.objectiveTargets.domain.stored=257;ProjectMapObjectives(s,map,2000);
    assert(!map.markers.domain.stored && map.markers.domain.availability==bridge::Availability::Incomplete);
    s.objectiveTargets.domain.stored=1;
    const auto alive=VisibleState(s,bridge::Page::Map,true,2000);
    const auto expired=VisibleState(s,bridge::Page::Map,true,2751);
    assert(alive!=expired);
    first.observedAt=2751;
    assert(VisibleState(s,bridge::Page::Map,true,2751)==alive); // Renewed observation reappears.
    first.observedAt=2760;
    assert(VisibleState(s,bridge::Page::Map,true,2761)==alive); // No repaint solely for observation timestamp.
    first.label.title={0xfffffff0,20};ProjectMapObjectives(s,map,2761);assert(!map.markers.domain.stored);
    first.label.title={1,1};first.observedAt=3000;s.sequence=1;
    MapObjectiveProjectionCache cache;
    cache.Project(s,map,3000);assert(map.markers.domain.stored==1);
    cache.Project(s,map,3100);assert(map.markers.domain.stored==1);
    cache.Project(s,map,3751);assert(!map.markers.domain.stored);
    first.observedAt=3800;++s.sequence;cache.Project(s,map,3800);assert(map.markers.domain.stored==1);
    map.acquired.value=false;cache.Project(s,map,3801);assert(!map.markers.domain.stored);
    map.acquired.value=true;cache.Project(s,map,3802);assert(map.markers.domain.stored==1);
    s.lifecycle=bridge::Lifecycle::NoPlayer;cache.Project(s,map,3803);assert(!map.markers.domain.stored);
    s.lifecycle=bridge::Lifecycle::Live;++s.generation;cache.Project(s,map,3804);assert(map.markers.domain.stored==1);
    first.label.title={2,0};++s.sequence;cache.Project(s,map,3805);assert(!map.markers.domain.stored);
    puts("PASS: cached projection expiry, acquisition/lifecycle invalidation and snapshot text-reference refresh");
    puts("PASS: acquired-map objective projection, optional markers, expiry/reappearance, unknown coordinates, lifecycle and schema bounds");
}

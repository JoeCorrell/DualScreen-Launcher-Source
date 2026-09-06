#include "State.h"
#include "Touch.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
using namespace ac2ds;
int main() {
    for (const char* bad : {"pinch .5 .5 nan", "tap .5 .5 garbage", "tap -1 0", "up 0 2", "move inf 0", "tap 0", "pinch 0 0 9", "tap 0 0 2"})
        assert(!ParseTouch(bad, std::strlen(bad)));
    const char embedded[]{'t','a','p',' ',0,'0',' ','0'}; assert(!ParseTouch(embedded, sizeof(embedded)));
    const char* pinch = "pinch 0.5 0.5 1.25"; assert(ParseTouch(pinch, std::strlen(pinch))->value == 1.25f);
    MapViewport map; map.Pinch(2, .5f, .5f); assert(map.zoom == 2 && map.x == .5f);
    map.Pan(100, -100); assert(map.x == .25f && map.y == .75f);
    map.Pinch(std::numeric_limits<float>::quiet_NaN(), 0, 0); assert(map.zoom == 2);
    map.Pinch(.001f, 0, 0); assert(map.zoom == 1 && map.x == .5f && map.y == .5f);
    StateStore store;
    Snapshot s; s.generation = 1; s.revision = 1; s.sampledAt = 1000; s.phase = Phase::Playing; s.capabilities = Inventory;
    Item item; item.id = 71; item.revision = 3; item.owned = true; item.actions = ActionBit(ActionKind::Use); s.items.push_back(item);
    assert(store.Publish(s)); auto pinned = store.Read();
    Command c{0,1,1,71,3,0,ActionKind::Use};
    assert(!store.Queue(c, 2000)); assert(!store.Queue({0,1,1,71,2,0,ActionKind::Use}, 1010));
    assert(!store.Queue({0,1,1,71,3,0,ActionKind::Equip}, 1010));
    assert(store.Queue(c, 1010)); assert(!store.Queue(c, 1011));
    auto first = store.Take(1012); assert(first && first->entity == 71 && !store.Take(1013));
    assert(store.Queue(c, 1014)); assert(!store.Take(1515));
    assert(!store.Publish(s)); s.revision++; s.phase = Phase::Loading; assert(store.Publish(s));
    assert(store.Read()->items.empty() && store.Read()->capabilities == 0); assert(pinned->items.size() == 1);
    assert(!store.Queue(c, 1020));
    s.generation = 2; s.revision = 1; s.phase = Phase::Playing; s.sampledAt = 1040; assert(store.Publish(s));
    assert(!store.Queue(c, 1041));
    s.revision++; s.capabilities |= Map; s.mapVisibilityVerified = true;
    Marker visible; visible.id = 1; visible.x = .2f; visible.y = .4f; visible.discovered = visible.visible = true;
    Marker hidden = visible; hidden.id = 2; hidden.discovered = false;
    s.markers = {visible, hidden}; assert(store.Publish(s)); assert(store.Read()->markers.size() == 1);
    s.revision++; s.mapVisibilityVerified = false; assert(store.Publish(s));
    assert(store.Read()->markers.empty() && !(store.Read()->capabilities & Map));
    std::cout << "AC II lifecycle, stale command, ownership and isolated touch contracts passed\n";
}

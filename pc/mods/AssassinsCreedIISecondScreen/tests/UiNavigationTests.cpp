#include "AnimusUi.h"
#include <cassert>
#include <iostream>
using namespace ac2ds;
int main() {
    Snapshot s;s.generation=1;s.revision=1;s.phase=Phase::Playing;s.capabilities=Database|Inventory;
    DatabaseEntry a;a.id=1;a.category=L"People";a.unlocked=true;
    DatabaseEntry b=a;b.id=2;
    DatabaseEntry c=a;c.id=3;c.category=L"Locations";
    DatabaseEntry locked=c;locked.id=4;locked.category=L"Secret";locked.unlocked=false;
    s.database={a,b,c,locked};
    UiState ui;ui.page=Page::Database;ui.Synchronize(s);
    assert(ui.category==L"People" && ui.selected==1);
    ui.selected=2;ui.detailScroll=100;ui.Synchronize(s);
    assert(ui.selected==2 && ui.detailScroll==100); // Keep the reader's place on updates.
    s.database[1].unlocked=false;ui.Synchronize(s);
    assert(ui.selected==1 && ui.detailScroll==0);
    s.database[0].unlocked=false;ui.Synchronize(s);
    assert(ui.category==L"Locations" && ui.selected==3);
    s.database[2].unlocked=false;ui.Synchronize(s);
    assert(ui.category.empty() && ui.selected==0 && ui.maximumScroll==0);
    Item item;item.id=10;item.revision=1;item.owned=true;
    s.items={item};ui.page=Page::Inventory;ui.selected=10;ui.Synchronize(s);
    assert(ui.selected==10);
    s.items.clear();ui.detailScroll=100;ui.pressedAction=10;ui.Synchronize(s);
    assert(ui.selected==0 && ui.detailScroll==0 && ui.pressedAction==0);
    StateStore store;s.items={item};assert(store.Publish(s));
    ui.selected=10;Touch tap{};tap.kind=TouchKind::Tap;tap.x=1000.f/Width;tap.y=908.f/Height;
    ui.TouchInput(tap,s,store,100);assert(ui.pressedAction==0); // No action on an inspect-only item.
    s.items[0].actions=ActionBit(ActionKind::Use);++s.revision;assert(store.Publish(s));
    ui.TouchInput(tap,s,store,1000);assert(ui.pressedAction==0); // Stale live snapshot rejected.
    ui.TouchInput(tap,s,store,100);assert(ui.pressedAction==10);
    ++s.generation;ui.Synchronize(s);assert(ui.pressedAction==0);
    std::cout << "UI navigation and action lifecycle tests passed\n";
}

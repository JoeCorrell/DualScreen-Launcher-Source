#include "NativeInventoryView.h"
#include <cassert>
#include <cstdio>
using namespace ac2ds;
using namespace ac2ds::native;
int main() {
    InventoryContents inventory;
    inventory.containers.push_back({1, 7, 1, 0, 5, true});
    inventory.containers.push_back({2, 11, 0, 9, 10, true});
    ReloadDefinition medicine{}, poison{};
    medicine.menuId = 14; medicine.category = 7; medicine.titleId = 100; medicine.descriptionId = 101;
    poison.menuId = 13; poison.category = 11; poison.locked = true;
    auto lookup = [](uint32_t id) -> std::optional<std::wstring> {
        if (id == 100) return L"MEDICINE";
        if (id == 101) return L"Original description";
        return {};
    };
    auto items = InventoryItems(inventory, {medicine, poison}, 23, lookup);
    assert(items.size() == 1 && items[0].name == L"MEDICINE");
    assert(items[0].description == L"Original description");
    assert(items[0].quantity == 0 && items[0].capacity == 5 && !items[0].owned);
    assert(items[0].actions == 0 && !items[0].equipped && !items[0].icon);
    assert(items[0].revision == 23 && items[0].id == 14);
    inventory.containers[0].quantity = 3;
    items = InventoryItems(inventory, {medicine, poison}, 24, lookup);
    assert(items.size() == 1 && items[0].quantity == 3 && items[0].owned && items[0].actions == 0);
    medicine.locked = true; assert(InventoryItems(inventory, {medicine, poison}, 25, lookup).empty());
    medicine.locked = false; medicine.titleId = 999;
    assert(InventoryItems(inventory, {medicine}, 26, lookup).empty());
    StateStore store; Snapshot s; s.phase = Phase::Playing; s.revision = 1;
    s.capabilities = Inventory; s.items = items; assert(store.Publish(s));
    Snapshot loading; loading.phase = Phase::Loading; loading.revision = 2; loading.items = items;
    assert(store.Publish(loading) && store.Read()->items.empty());
    std::puts("Inventory projection: ownership, unlocks, native text and transition tests passed");
}

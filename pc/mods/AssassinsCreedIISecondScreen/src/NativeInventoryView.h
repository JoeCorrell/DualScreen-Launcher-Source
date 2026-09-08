#pragma once
#include "NativeInventoryMenu.h"
#include "State.h"

namespace ac2ds::native {
// Projection contains only traced reload categories. Do not infer armor,
// weapons, health, ownership or equipped state from other container numbers.
template<class Lookup>
std::vector<Item> InventoryItems(const InventoryContents& inventory,
        const std::vector<ReloadDefinition>& definitions, uint64_t revision, Lookup lookup) {
    std::vector<Item> items;
    for (uint32_t id : {11u, 13u, 12u, 10u, 14u}) {
        for (const auto& definition : definitions) {
            if (definition.menuId != id || definition.locked) continue;
            for (const auto& container : inventory.containers) {
                if (container.category != definition.category) continue;
                auto title = lookup(definition.titleId);
                if (!title || title->empty()) continue;
                Item item;
                item.id = id; item.revision = revision;
                item.name = *title; item.category = *title;
                auto description = lookup(definition.descriptionId);
                if (description) item.description = *description;
                item.quantity = container.quantity; item.capacity = container.capacity;
                item.owned = container.quantity > 0;
                item.details = L"Quantity: " + std::to_wstring(container.quantity) + L" / " + std::to_wstring(container.capacity);
                item.artworkId = definition.artworkId; item.thumbnailId = definition.thumbnailId;
                // Native actions remain gated separately from read-only artwork.
                items.push_back(std::move(item));
            }
        }
    }
    return items;
}
}

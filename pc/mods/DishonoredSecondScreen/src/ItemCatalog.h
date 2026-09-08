#pragma once
#include <cstdint>
#include <string_view>

namespace dishonored {
struct ItemAsset { std::u16string_view native; const wchar_t* file{}; };
inline constexpr ItemAsset ItemAssets[] = {
#define DIS_ITEM_ASSET(name) {u"" #name, L"items\\" L"" #name L"_Large.png"},
#include "ItemAssets.inc"
#undef DIS_ITEM_ASSET
};
// Native C462A0 builds img://<package>.<JournalIconName>_Large.
// Only exact extracted base-game icon names resolve; unknown names do not
// become arbitrary paths or substitute another item's appearance.
inline uint64_t ItemArtwork(std::u16string_view native) noexcept {
    for (uint32_t i = 0; i < sizeof(ItemAssets) / sizeof(ItemAssets[0]); ++i)
        if (ItemAssets[i].native == native) return 1000 + i;
    return 0;
}
} // namespace dishonored

#pragma once
#include <cstdint>
#include <string_view>
namespace dishonored {
struct PortraitAsset { std::u16string_view object; const wchar_t* file{}; };
inline constexpr PortraitAsset PortraitAssets[] = {
#define DIS_PORTRAIT_ASSET(name) {u"UI_portraits_objectives." u"" #name, L"portraits\\" L"" #name L".png"},
#include "PortraitAssets.inc"
#undef DIS_PORTRAIT_ASSET
};
inline uint64_t PortraitArtwork(std::u16string_view object) noexcept {
    for (uint32_t i = 0; i < sizeof(PortraitAssets) / sizeof(PortraitAssets[0]); ++i)
        if (PortraitAssets[i].object == object) return 3000 + i;
    return 0;
}
} // namespace dishonored

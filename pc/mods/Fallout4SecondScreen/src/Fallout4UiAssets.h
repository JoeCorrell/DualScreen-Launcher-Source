#pragma once

#include <windows.h>

#include <cstdint>

namespace fo4ds {

struct Fallout4UiAssetBox {
  int x;
  int y;
  int w;
  int h;
};

// These draw only installer-generated raster caches derived locally from the
// user's own Fallout 4 Interface.ba2. No Bethesda artwork is linked into the
// proxy or shipped in the source/package.
bool DrawFallout4SpecialArt(HDC target, uint32_t special_index,
                            const Fallout4UiAssetBox& bounds);
bool DrawFallout4ConditionArt(HDC target, uint32_t body_flags,
                              uint32_t head_flags,
                              const Fallout4UiAssetBox& bounds);
bool DrawFallout4PerkArt(HDC target, uint32_t clip_name, uint32_t form_id,
                         const char* swf_file,
                         const Fallout4UiAssetBox& bounds);

void ShutdownFallout4UiAssets();

}  // namespace fo4ds

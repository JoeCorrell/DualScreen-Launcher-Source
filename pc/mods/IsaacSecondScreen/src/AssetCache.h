#pragma once

#include <windows.h>
#include <wtypes.h>
#include <gdiplus.h>

#include <string>

#include "AssetCatalog.h"

namespace isaacds {

bool InitializeAssetCache(HMODULE module);
void ShutdownAssetCache();
const std::wstring& AssetRootPath();
Gdiplus::Image* ResolveAsset(const std::string& relative_path);
bool DrawAssetStretch(HDC dc, const std::string& relative_path,
                      const RECT& bounds, bool smooth = true);
bool DrawAssetCover(HDC dc, const std::string& relative_path,
                    const RECT& bounds, bool smooth = true);
bool DrawAssetFit(HDC dc, const std::string& relative_path,
                  const RECT& bounds, bool smooth = false);
bool DrawAssetRegion(HDC dc, const std::string& relative_path,
                     const RECT& source_bounds, const RECT& destination,
                     bool smooth = false);
bool DrawSpriteColor(HDC dc, const SpriteFrame* frame, const RECT& bounds, float red, float green, float blue, float alpha);
bool DrawSprite(HDC dc, const SpriteFrame* frame, const RECT& bounds, bool red_room = false);

}  // namespace isaacds

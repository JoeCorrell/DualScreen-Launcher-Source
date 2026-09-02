#pragma once

#include <windows.h>

namespace wfcds {

bool InitializeAssetCache(HMODULE module);
void ShutdownAssetCache();

// Draws a cached, aspect-fit PNG from assets/<key>.png. Keys are deliberately
// restricted to a single filename so live game metadata cannot escape the
// mod's asset directory.
bool DrawCachedAsset(HDC dc, const char* key, const RECT& bounds);
// Draws an aspect-fill PNG clipped to the requested bounds. This is used for
// native chapter artwork that acts as a background rather than an icon.
bool DrawCachedAssetCover(HDC dc, const char* key, const RECT& bounds);

}  // namespace wfcds

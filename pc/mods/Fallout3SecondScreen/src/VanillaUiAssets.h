#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace fo3ds {

// Rectangle form that can be constructed directly from the panel's Box
// members without exposing Panel.cpp's private drawing types.
struct UiAssetBox {
  int x;
  int y;
  int w;
  int h;
};

struct VanillaUiAssetCacheStats {
  size_t cached_assets;
  size_t cached_misses;
  size_t cached_tints;
  uint64_t cached_bytes;
  uint64_t file_reads;
  uint64_t dds_decodes;
};

// game_directory is the directory containing Fallout3.exe. Passing nullptr
// derives it from the running process. Initialization only reads the archive
// directory; individual DDS payloads are loaded lazily and cached.
bool InitializeVanillaUiAssets(const wchar_t* game_directory = nullptr);

// Loads and decodes an asset without drawing it. Paths are relative to Data,
// for example: textures\interface\icons\stats\special_strength.dds.
bool PreloadVanillaUiAsset(const wchar_t* game_relative_path);

// The default tint matches the panel's Pip-Boy green. RGB(255,255,255) keeps
// the source colors. opacity is combined with the DDS per-pixel alpha.
bool DrawVanillaUiAsset(HDC target, const wchar_t* game_relative_path,
                        const RECT& destination,
                        COLORREF tint = RGB(86, 246, 146),
                        BYTE opacity = 255);
bool DrawVanillaUiAsset(HDC target, const wchar_t* game_relative_path,
                        const UiAssetBox& destination,
                        COLORREF tint = RGB(86, 246, 146),
                        BYTE opacity = 255);

// Draws the whole source image without stretching it out of proportion.
// Transparent padding is preserved and the result is centered in bounds.
bool DrawVanillaUiAssetFit(HDC target, const wchar_t* game_relative_path,
                           const UiAssetBox& bounds,
                           COLORREF tint = RGB(86, 246, 146),
                           BYTE opacity = 255);

// Aspect-fits the visible pixels instead of the complete DDS canvas. Fallout
// inventory icons commonly contain generous transparent margins; fitting the
// full canvas makes the actual Vault Boy artwork needlessly small. The source
// crop is calculated once when the DDS enters the persistent asset cache.
bool DrawVanillaUiAssetContentFit(HDC target,
                                  const wchar_t* game_relative_path,
                                  const UiAssetBox& bounds,
                                  COLORREF tint = RGB(86, 246, 146),
                                  BYTE opacity = 255);

// Draws the visible source content about the center of bounds using the
// destination DC's world transform. angle_degrees follows Fallout's clockwise
// UI rotation convention. This is used for the original map cursor rather
// than replacing it with custom line geometry.
bool DrawVanillaUiAssetContentFitRotated(
    HDC target, const wchar_t* game_relative_path, const UiAssetBox& bounds,
    float angle_degrees, COLORREF tint = RGB(86, 246, 146),
    BYTE opacity = 255);

// Uses a family-wide visible-content extent for scale while centering each
// asset by its own alpha bounds. Assets in a Pip-Boy artwork family therefore
// keep one scale as selection changes instead of visibly jumping in size.
bool DrawVanillaUiAssetContentFramed(
    HDC target, const wchar_t* game_relative_path, const UiAssetBox& bounds,
    int family_content_width, int family_content_height,
    COLORREF tint = RGB(86, 246, 146), BYTE opacity = 255);

// Draws the original DDS canvas at an exact source-pixel scale. Fallout's CND
// body parts use power-of-two canvases but position their visible pixels in a
// shared XML coordinate tree; independently stretching each canvas breaks the
// face, shoulders and limbs apart. This preserves that authored coordinate
// relationship while still allowing the complete tree to be scaled once.
bool DrawVanillaUiAssetPixelScaled(
    HDC target, const wchar_t* game_relative_path, int destination_x,
    int destination_y, float destination_pixels_per_source_pixel,
    COLORREF tint = RGB(86, 246, 146), BYTE opacity = 255);

// Draws a normalized crop of the installed DDS. This keeps the world map's
// texture, player and markers on one transform while pan/zoom changes the
// visible source region. Coordinates are clamped to [0,1].
bool DrawVanillaUiAssetRegion(HDC target,
                              const wchar_t* game_relative_path,
                              const UiAssetBox& destination,
                              float source_left, float source_top,
                              float source_right, float source_bottom,
                              COLORREF tint = RGB(86, 246, 146),
                              BYTE opacity = 255);

VanillaUiAssetCacheStats GetVanillaUiAssetCacheStats();

// Clear keeps the parsed read-only BSA directory. Shutdown also closes the
// archive and forgets the game path. Both release every cached GDI object.
void ClearVanillaUiAssetCache();
void ShutdownVanillaUiAssets();

}  // namespace fo3ds

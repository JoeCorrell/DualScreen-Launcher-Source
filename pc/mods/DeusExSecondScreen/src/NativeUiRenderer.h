// Native Deus Ex texture compositing for the lower-screen UI.
//
// The inventory artwork already loaded by DXHR is higher quality and more
// faithful than a second, copied asset set.  This renderer resolves those
// RenderResources in-place and draws them into the panel swapchain.  It does
// not take ownership of game objects and it restores every D3D11 state slot it
// changes before returning to the game.

#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

namespace dxs {

struct NativeUiRect {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

struct NativeUiUvRect {
  float left = 0.0f;
  float top = 0.0f;
  float right = 1.0f;
  float bottom = 1.0f;
};

struct NativeUiColor {
  float red = 1.0f;
  float green = 1.0f;
  float blue = 1.0f;
  float alpha = 1.0f;
};

struct NativeTextureInfo {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t texture_id = 0;
};

class NativeUiRenderer {
 public:
  NativeUiRenderer();
  ~NativeUiRenderer();

  NativeUiRenderer(const NativeUiRenderer&) = delete;
  NativeUiRenderer& operator=(const NativeUiRenderer&) = delete;

  // Re-initializing with the same device/context is harmless.  A device
  // change releases all cached views and rebuilds the renderer.
  bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
  void Shutdown();

  // Begin/End form one lower-panel overlay batch.  Coordinates are panel
  // pixels with (0,0) at the top-left.
  bool Begin(ID3D11RenderTargetView* target, float width, float height);
  bool End();

  // Resolve a DTP Pickup and draw its normal or transposed inventory artwork.
  // `transposed` selects the same texture variant used by the game's grid.
  bool DrawPickup(uint32_t pickup_dtp_id, bool transposed,
                  const NativeUiRect& destination,
                  const NativeUiColor& color = NativeUiColor(),
                  NativeTextureInfo* info = nullptr);

  // Draw any texture from the game's RenderResource section by domain ID.
  bool DrawTexture(uint32_t texture_id, const NativeUiRect& destination,
                   const NativeUiColor& color = NativeUiColor(),
                   const NativeUiUvRect& uv = NativeUiUvRect(),
                   NativeTextureInfo* info = nullptr);

  // Queue a view already owned by the caller.  The renderer takes a temporary
  // reference through End(), which also makes panel resize/device-loss paths
  // safe.
  bool DrawView(ID3D11ShaderResourceView* view,
                const NativeUiRect& destination,
                const NativeUiColor& color = NativeUiColor(),
                const NativeUiUvRect& uv = NativeUiUvRect(),
                float clockwise_degrees = 0.0f);

  // Small procedural masks used for live map contacts and locked augmentation
  // overlays. They remain in this renderer so they composite after native art
  // without depending on an unrelated game texture ID.
  bool DrawCircle(const NativeUiRect& destination,
                  const NativeUiColor& color = NativeUiColor());
  bool DrawDiamond(const NativeUiRect& destination,
                   const NativeUiColor& color = NativeUiColor());
  bool DrawPlayerArrow(const NativeUiRect& destination, float clockwise_degrees,
                       const NativeUiColor& color = NativeUiColor());
  bool DrawLock(const NativeUiRect& destination,
                const NativeUiColor& color = NativeUiColor());
  // Compact gameplay reticle used on the primary display after the legacy
  // Scaleform HUD root is suppressed. It intentionally contains no dashboard
  // information: the crosshair remains above while every other HUD element
  // lives on the lower screen.
  bool DrawReticle(const NativeUiRect& destination,
                   const NativeUiColor& color = NativeUiColor());

  uint32_t queued() const;
  uint32_t rendered_last_frame() const;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

NativeUiRenderer& TheNativeUiRenderer();

}  // namespace dxs

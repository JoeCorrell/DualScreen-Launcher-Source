// The bottom screen, as somewhere to draw.
//
// Under WEMU this is a second guest window, and that is not an implementation
// choice -- it is the contract. SecondScreenOutput.claims() tests a window's
// top-left corner against the THOR-BOTTOM rectangle and hands a window that
// qualifies straight to SurfaceFlinger through an ASurfaceControl, bypassing
// the compositor entirely and costing no GPU time. A window that does not
// qualify is composited into the top panel and clipped away, so "one tall
// window covering both screens" is not an option that exists.
//
// The window is created at the second monitor's origin for exactly that reason.
// Everything else here follows from having to own a swapchain: a render target
// view for the mod to draw into, a present, and enough care around resize and
// device loss that a resolution change does not leave a dead surface behind.

#pragma once

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>

struct IUnknown;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
struct IDXGIFactory;
struct IDXGISwapChain;

namespace dis {

class Panel {
 public:
  // `create_swapchain` is the factory's original CreateSwapChain, not the
  // hooked one: the panel's own swapchain must never be mistaken for the
  // game's by the code that is watching for the game's.
  using CreateSwapChainFn = long(__stdcall*)(IDXGIFactory*, IUnknown*, void*, IDXGISwapChain**);

  // The panel's buffer must share the game's back-buffer format, because
  // CopySubresourceRegion refuses a format mismatch outright. Set before Create.
  void SetFormat(unsigned dxgi_format) { format_ = dxgi_format; }

  // Copies a rectangle of `source` into the panel at (dest_x, dest_y).
  // No scaling: CopySubresourceRegion cannot scale, which is precisely why it
  // is cheap -- it is a GPU-side blit of exactly the pixels asked for, not a
  // second pass over the scene.
  bool CopyFrom(ID3D11Texture2D* source, int sx, int sy, int sw, int sh,
                int dest_x, int dest_y);

  bool Create(IDXGIFactory* factory, ID3D11Device* device, ID3D11DeviceContext* context,
              CreateSwapChainFn create_swapchain);
  void Destroy();

  bool ready() const;
  int width() const { return width_; }
  int height() const { return height_; }
  HWND window() const { return window_; }
  // Returns an AddRef'ed view. Callers release it after their command has
  // taken its own reference; this makes resize safe across render threads.
  ID3D11RenderTargetView* AcquireRtv() const;
  IDXGISwapChain* swapchain() const { return swapchain_; }

  // Clears to the interface's own near-black. Does not bind anything: the clear
  // takes the view as an argument, so the game's output-merger state is not
  // read and not disturbed.
  void Clear();

  // Same operation with an explicit clear colour. Calibration uses this to
  // establish a known background before testing whether a vtable slot draws.
  void ClearTo(float r, float g, float b, float a);

  // Clears a new native-UI frame and composites the persistent tab strip on
  // the renderer thread before Scaleform draws into the content area.
  void PrepareUiFrame(ID3D11DeviceContext* context);

  // Draws the custom lower-panel HUD. This is the fallback path used when the
  // native UI renderer cannot be safely redirected.
  void PrepareCustomHud(ID3D11DeviceContext* context, float health, float stamina);

  // Uploads an already-rasterised RGBA image to the back buffer.
  //
  // This is the path the real interface uses. It exists separately from
  // PrepareCustomHud because that one builds a DIB, draws into it and throws it
  // away on every frame; the interface keeps one canvas for the life of the
  // process and only repaints when something changed, so all that is left to do
  // here is the copy. `rgba` must be width()*height() pixels, four bytes each.
  bool UploadPixels(ID3D11DeviceContext* context, const void* rgba,
                    int width, int height);

  bool Present();

  // Handles the no-activate window's tab strip. Returns true when the message
  // belongs to the panel UI and must not reach DefWindowProc.
  bool HandlePointer(UINT message, int x, int y);
  bool HandleNormalizedTap(float x, float y);
  bool HandleNormalizedHold(float x, float y);
  bool HandleNormalizedPinch(float x, float y, float factor);
  bool HandleNormalizedPointer(UINT message, float x, float y);
  void CancelPointer();

  // Called when the game's own swapchain resizes, because on the Thor that is
  // the moment the panel's geometry may also have changed.
  void OnDisplayChanged();

  uint32_t presents() const { return presents_; }
  long last_error() const { return last_error_; }

 private:
  bool CreateSurface();
  void ReleaseSurface();
  bool ResolveRect(RECT* out);
  bool EnsureChrome();
  void DrawChrome(ID3D11DeviceContext* context);
  void ActivateTab(int index);

  IDXGIFactory* factory_ = nullptr;
  ID3D11Device* device_ = nullptr;
  ID3D11DeviceContext* context_ = nullptr;
  CreateSwapChainFn create_swapchain_ = nullptr;

  HWND window_ = nullptr;
  IDXGISwapChain* swapchain_ = nullptr;
  ID3D11RenderTargetView* rtv_ = nullptr;
  ID3D11Texture2D* back_buffer_ = nullptr;
  ID3D11Texture2D* chrome_texture_ = nullptr;
  mutable std::mutex rtv_lock_;
  int width_ = 0;
  int height_ = 0;
  uint32_t presents_ = 0;
  long last_error_ = 0;
  long logged_present_error_ = 0;
  uint32_t repeated_present_errors_ = 0;
  bool found_second_monitor_ = false;
  unsigned format_ = 28;  // DXGI_FORMAT_R8G8B8A8_UNORM
  int chrome_height_ = 0;
  std::atomic<int> active_tab_{0};
  std::atomic<bool> chrome_dirty_{true};
  std::atomic<bool> surface_ready_{false};
};

Panel& ThePanel();

}  // namespace dis

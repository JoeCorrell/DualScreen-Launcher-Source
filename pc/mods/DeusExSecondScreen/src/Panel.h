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
#include <vector>

#include "CustomUi.h"
#include "NativeMap.h"

struct IUnknown;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
struct IDXGIFactory;
struct IDXGISwapChain;

namespace dxs {

enum class TouchAction {
  kTap,
  kDown,
  kMove,
  kUp,
  kCancel,
  kHold,
  kPinch,
};

class Panel {
 public:
  // `create_swapchain` is the factory's original CreateSwapChain, not the
  // hooked one: the panel's own swapchain must never be mistaken for the
  // game's by the code that is watching for the game's.
  using CreateSwapChainFn = long(__stdcall*)(IDXGIFactory*, IUnknown*, void*, IDXGISwapChain**);

  bool Create(IDXGIFactory* factory, ID3D11Device* device, ID3D11DeviceContext* context,
              CreateSwapChainFn create_swapchain);
  void Destroy();

  bool ready() const;
  bool game_loaded() const {
    return game_loaded_.load(std::memory_order_acquire);
  }
  // The panel window exists early so WEMU can claim its swapchain, but its
  // buffer stays blank until a real in-game state proves a save is playable.
  void SetGameLoaded(bool loaded, const char* evidence);
  // Whole-interface routing exposes one anonymous Scaleform root, so named
  // HUD controllers cannot be the only save-loaded signal. The native menu
  // input lease is acquired by title/save UI and released into gameplay.
  void ObserveGameState(bool menu_input_locked);
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

  // Clears a new native-UI frame and composites the persistent tab strip on
  // the renderer thread before Scaleform draws into the content area.
  void PrepareUiFrame(ID3D11DeviceContext* context);

  bool Present();

  // Handles the no-activate window's tab strip. Returns true when the message
  // belongs to the panel UI and must not reach DefWindowProc.
  bool HandlePointer(UINT message, int x, int y);
  bool HandleNormalizedTap(float x, float y);
  bool SelectTab(int index);
  int active_tab() const {
    return active_tab_.load(std::memory_order_acquire);
  }
  // True while the game owns a real modal/menu input lease. The custom pages
  // never acquire this lease; it is used only to keep hacking, pause, dialogue,
  // and other native modal UI visible on the upper display.
  bool menu_input_locked() const {
    return menu_input_locked_.load(std::memory_order_acquire);
  }
  bool weapon_equipped() const {
    return weapon_equipped_.load(std::memory_order_acquire);
  }
  void ObserveMenuInputState(bool locked);
  bool HandleNormalizedTouch(TouchAction action, float x, float y,
                             float value = 0.0f);

  // Called when the game's own swapchain resizes, because on the Thor that is
  // the moment the panel's geometry may also have changed.
  void OnDisplayChanged();
  // Recreates the HWND and auxiliary swapchain if DXVK moves primary Present
  // to another thread after a renderer reset.
  bool RehomeToCurrentThread();

  uint32_t presents() const { return presents_; }
  long last_error() const { return last_error_; }

 private:
  bool CreateSurface();
  void ReleaseSurface();
  bool ResolveRect(RECT* out);
  bool EnsureChrome();
  bool EnsureChromeGdiSurface();
  void ReleaseChromeGdiSurface();
  bool EnsureHudSnapshot();
  // Applies visibility changes on the primary Present thread. Scaleform can
  // report menu/gameplay evidence from its renderer worker, where touching a
  // swapchain or the immediate context is unsafe.
  void ApplyGameLoadedTransition();
  void DrawChrome(ID3D11DeviceContext* context);
  void ComposeHud(ID3D11DeviceContext* context);
  void ComposePageChrome(ID3D11DeviceContext* context);
  void ActivateTab(int index);
  void RefreshLiveModels();
  void ReconcileInventoryUiSlots();
  bool MoveInventoryUiItem(int item_index, int destination_slot);
  bool SortInventoryUiSlots();
  void DrawNativeUiIcons();
  bool HandleCustomTouch(TouchAction action, int x, int y, float value);

  IDXGIFactory* factory_ = nullptr;
  ID3D11Device* device_ = nullptr;
  ID3D11DeviceContext* context_ = nullptr;
  CreateSwapChainFn create_swapchain_ = nullptr;

  HWND window_ = nullptr;
  DWORD window_owner_thread_ = 0;
  std::vector<HWND> retired_windows_;
  uint64_t retired_window_retry_ms_ = 0;
  IDXGISwapChain* swapchain_ = nullptr;
  ID3D11RenderTargetView* rtv_ = nullptr;
  ID3D11Texture2D* back_buffer_ = nullptr;
  ID3D11Texture2D* chrome_texture_ = nullptr;
  ID3D11RenderTargetView* chrome_rtv_ = nullptr;
  HDC chrome_dc_ = nullptr;
  HBITMAP chrome_bitmap_ = nullptr;
  HGDIOBJ chrome_old_bitmap_ = nullptr;
  void* chrome_dib_bits_ = nullptr;
  int chrome_gdi_width_ = 0;
  int chrome_gdi_height_ = 0;
  std::vector<uint32_t> chrome_upload_pixels_;
  ID3D11Texture2D* hud_snapshot_ = nullptr;
  mutable std::mutex rtv_lock_;
  int width_ = 0;
  int height_ = 0;
  uint32_t presents_ = 0;
  long last_error_ = 0;
  bool found_second_monitor_ = false;
  int chrome_height_ = 0;
  // The reference interface opens on Inventory. There is deliberately no
  // synthetic "HUD page": the five tabs are independent lower-screen views
  // and never open a vanilla pause menu on the top display.
  std::atomic<int> active_tab_{3};
  std::atomic<bool> chrome_dirty_{true};
  std::atomic<bool> surface_ready_{false};
  std::atomic<bool> game_loaded_{false};
  std::atomic<bool> game_visibility_dirty_{false};
  // Loading and gameplay movies can overlap during a crossfade. Recent
  // loading evidence briefly wins so the lower display cannot flash.
  std::atomic<uint32_t> show_after_tick_{0};
  std::atomic<bool> menu_input_locked_{false};
  std::atomic<bool> weapon_equipped_{false};
  bool initial_menu_lock_seen_ = false;
  uint64_t gameplay_unlock_started_ms_ = 0;
  uint32_t gameplay_ready_samples_ = 0;
  bool blank_frame_presented_ = false;
  bool ui_present_pending_ = true;
  uint64_t last_ui_present_ms_ = 0;
  // Header targets use -2/-3 for the carousel arrows and non-negative page
  // ids.  Keep a distinct sentinel so a press in the header's empty padding
  // is still owned until Up and cannot turn into a content tap mid-gesture.
  int pressed_header_target_ = -4;
  bool touch_button_down_ = false;
  bool touch_dragged_ = false;
  int selected_item_ = -1;
  InventoryActionIdentity selected_item_identity_;
  int pressed_item_ = -1;
  InventoryActionIdentity pressed_item_identity_;
  int drag_item_ = -1;
  InventoryActionIdentity drag_item_identity_;
  int pressed_quick_slot_ = -1;
  int selected_aug_ = 0;
  int selected_aug_node_ = -1;
  int selected_objective_ = 0;
  int selected_media_ = 0;
  int inventory_page_ = 0;
  std::vector<int> inventory_ui_slots_;
  std::vector<uint32_t> inventory_ui_pickups_;
  int augmentation_page_ = 0;
  int objective_page_ = 0;
  int media_page_ = 0;
  float touch_down_x_ = 0.0f;
  float touch_down_y_ = 0.0f;
  float touch_last_x_ = 0.0f;
  float touch_last_y_ = 0.0f;
  // Start at the authored map page's maximum zoom. The view remains centered
  // on Jensen until the player deliberately pans it.
  float map_zoom_ = 3.0f;
  float map_pan_x_ = 0.0f;
  float map_pan_y_ = 0.0f;
  bool map_follow_player_ = true;
  uint32_t map_floor_map_id_ = 0;
  int map_display_floor_ = 0;
  bool map_floor_manual_ = false;
  uint64_t last_drag_animation_ms_ = 0;
  uint64_t last_map_animation_ms_ = 0;
  uint64_t surface_retry_ms_ = 0;
  uint64_t last_model_poll_ms_ = 0;
  uint64_t last_map_poll_ms_ = 0;
  uint64_t last_map_pose_poll_ms_ = 0;
  // A save/menu handoff can leave the old GlobalPlayerInfo pointer detached
  // before any named loading movie reaches the Scaleform router. Require a
  // sustained failure while the retail menu owns input before hiding the UI;
  // brief hacking/dialogue transitions must not be mistaken for an unload.
  uint64_t inventory_unavailable_since_ms_ = 0;
  uint32_t consecutive_inventory_capture_failures_ = 0;
  uint32_t consecutive_map_capture_failures_ = 0;
  uint64_t last_model_diagnostic_ms_ = 0;
  uint64_t last_icon_diagnostic_ms_ = 0;
  uint64_t native_artwork_ready_ms_ = 0;
  uint64_t native_icon_retry_ms_ = 0;
  uint32_t native_icon_retry_delay_ms_ = 1000;
  bool native_artwork_delay_reported_ = false;
  int reported_icon_requests_ = -1;
  int reported_icon_draws_ = -1;
  uint64_t rendered_model_revision_ = 0;
  // Capture scratch is Panel-owned rather than a large Present-thread local.
  // InventorySnapshot includes the complete native augmentation state and the
  // inventory catalog, so putting it on DXHR's game-thread stack is unsafe.
  InventorySnapshot inventory_staging_;
  InventorySnapshot inventory_snapshot_;
  NativeMapSnapshot native_map_staging_;
  NativeMapSnapshot native_map_snapshot_;
  SmartGlassSnapshot smartglass_snapshot_;
  CustomUiFrame custom_ui_frame_;
  CustomHit pressed_hit_;
};

Panel& ThePanel();

}  // namespace dxs

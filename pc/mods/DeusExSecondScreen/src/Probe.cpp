// Bringing the mod up, and keeping it honest.
//
// The order matters and is not arbitrary. The DLL is loaded while d3d11.dll is
// still being mapped, long before the game has a device, so nothing can happen
// at load time. The first real anchor is the DXGI factory; the first moment
// everything exists at once -- device, context, swapchain, and a Scaleform
// renderer that has been through a display pass -- is the first Present. On
// native Windows the survey and panel can be created while that Present is in
// flight. DXVK must first complete one primary-swapchain Present of its own, so
// panel creation is deliberately deferred until the original call returns.
//
// Every run writes DeusExSecondScreen.log, and the first thing in it is what
// this build measured rather than what it assumed: the ASLR slide, the vtable
// addresses actually found, the class behind the render global, and whether
// BeginDisplay turned out to be per element or per frame. The last of those
// decides whether the mod can do its job at all, so it is reported plainly
// rather than inferred from a blank screen.

#include "Probe.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <xinput.h>

#include <atomic>
#include <cstdint>

#include "Config.h"
#include "GameActions.h"
#include "Hook.h"
#include "Layout.h"
#include "Log.h"
#include "Mem.h"
#include "NativeMap.h"
#include "NativeUiRenderer.h"
#include "Panel.h"
#include "Rtti.h"
#include "Scaleform.h"
#include "SmartGlass.h"
#include "TouchInput.h"

namespace dxs {
namespace {

// Preferred-base addresses recovered by static analysis, kept for the log so a
// run against a different build says so instead of misbehaving quietly.
constexpr uintptr_t kRenderGlobalVA = 0x00EAAB48;
constexpr uintptr_t kPlacementVA = 0x0072F8E0;
constexpr uintptr_t kBeginDisplayVA = 0x007328B0;
constexpr uintptr_t kApplyVA = 0x00733F80;
// DXHRDC resolves XInputGetState dynamically at 0x004B38A3 and stores the
// result here. The optional diagnostic below can hook that game-owned slot
// without intercepting Wine, WEMU, or another process-wide copy of XInput.
// Normal builds deliberately leave the controller untouched.
constexpr uintptr_t kXInputGetStatePointerVA = 0x00E514C4;

constexpr uint32_t kFactoryCreateSwapChain = 10;         // IDXGIFactory
constexpr uint32_t kFactoryCreateSwapChainForHwnd = 15;  // IDXGIFactory2
constexpr uint32_t kSwapChainPresent = 8;                // IDXGISwapChain
constexpr uint32_t kSwapChainResizeBuffers = 13;
constexpr uint32_t kSwapChainPresent1 = 22;              // IDXGISwapChain1

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, const void*);
using ResizeFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using CreateSwapChainFn = HRESULT(__stdcall*)(IDXGIFactory*, IUnknown*,
                                              DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateForHwndFn = HRESULT(__stdcall*)(IDXGIFactory*, IUnknown*, HWND, const void*,
                                            const void*, IDXGIOutput*, IDXGISwapChain**);
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

PresentFn o_Present = nullptr;
Present1Fn o_Present1 = nullptr;
ResizeFn o_ResizeBuffers = nullptr;
CreateSwapChainFn o_CreateSwapChain = nullptr;
CreateForHwndFn o_CreateForHwnd = nullptr;
XInputGetStateFn o_XInputGetState = nullptr;

RttiIndex g_rtti;
HookSet g_hooks;

IDXGIFactory* g_factory = nullptr;
IDXGISwapChain* g_swapchain = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11Texture2D* g_game_back_buffer = nullptr;
ID3D11RenderTargetView* g_game_rtv = nullptr;
UINT g_game_back_width = 0;
UINT g_game_back_height = 0;
HWND g_game_window = nullptr;

std::atomic<bool> g_factory_seen{false};
std::atomic<bool> g_started{false};
std::atomic<bool> g_initialization_attempted{false};
std::atomic<bool> g_ready{false};
std::atomic<uint32_t> g_frame{0};
// All mutable renderer/panel service state belongs to the thread that first
// presents DXHR's primary swapchain. The lock also makes swapchain publication
// atomic and keeps ResizeBuffers from releasing panel resources mid-frame.
SRWLOCK g_frame_lock = SRWLOCK_INIT;
std::atomic<DWORD> g_service_thread{0};
std::atomic<uint64_t> g_service_thread_last_present_ms{0};
std::atomic<bool> g_display_change_pending{false};
std::atomic<bool> g_panel_rehome_required{false};
std::atomic<uint64_t> g_panel_rehome_retry_ms{0};

constexpr uintptr_t kShowReticleVA = 0x01C79ED9;

void ReleaseGameSurface() {
  if (g_game_rtv) { g_game_rtv->Release(); g_game_rtv = nullptr; }
  if (g_game_back_buffer) {
    g_game_back_buffer->Release();
    g_game_back_buffer = nullptr;
  }
  g_game_back_width = 0;
  g_game_back_height = 0;
}

bool EnsureGameSurface(IDXGISwapChain* chain) {
  if (g_game_rtv && g_game_back_buffer && g_game_back_width &&
      g_game_back_height) return true;
  if (!chain || !g_device) return false;
  ID3D11Texture2D* back = nullptr;
  if (FAILED(chain->GetBuffer(0, IID_ID3D11Texture2D,
                              reinterpret_cast<void**>(&back))) || !back) {
    return false;
  }
  D3D11_TEXTURE2D_DESC desc = {};
  back->GetDesc(&desc);
  ID3D11RenderTargetView* view = nullptr;
  const HRESULT hr = g_device->CreateRenderTargetView(back, nullptr, &view);
  if (FAILED(hr) || !view || !desc.Width || !desc.Height) {
    if (view) view->Release();
    back->Release();
    return false;
  }
  g_game_back_buffer = back;
  g_game_rtv = view;
  g_game_back_width = desc.Width;
  g_game_back_height = desc.Height;
  DXS_INFO("[hud] primary crosshair surface ready (%ux%u)",
           g_game_back_width, g_game_back_height);
  return true;
}

void DrawPreservedCrosshair(IDXGISwapChain* chain) {
  // Drawing through the panel renderer into DXVK's primary back buffer is not
  // safe on WEMU. It can terminate the guest immediately after the first live
  // lower-screen frame. Keep the experimental path available for desktop
  // diagnostics, but never enter it unless explicitly requested.
  if (!Cfg().GetBool("hud.custom_crosshair", false)) return;
  Panel& panel = ThePanel();
  if (!g_ready.load(std::memory_order_acquire) || !panel.game_loaded() ||
      panel.menu_input_locked() || !panel.weapon_equipped()) return;
  uint8_t show_reticle = 1;
  Read(reinterpret_cast<const void*>(Rebase(kShowReticleVA)), &show_reticle);
  if (!show_reticle || !EnsureGameSurface(chain)) return;

  NativeUiRenderer& renderer = TheNativeUiRenderer();
  if (!renderer.Begin(g_game_rtv, static_cast<float>(g_game_back_width),
                      static_cast<float>(g_game_back_height))) return;
  const float size = (std::max)(28.0f,
      (std::min)(52.0f, static_cast<float>(g_game_back_height) * 0.050f));
  const float cx = static_cast<float>(g_game_back_width) * 0.5f;
  const float cy = static_cast<float>(g_game_back_height) * 0.5f;
  renderer.DrawReticle(NativeUiRect{cx - size * 0.5f, cy - size * 0.5f,
                                    cx + size * 0.5f, cy + size * 0.5f},
                       NativeUiColor{0.91f, 0.94f, 0.86f, 0.92f});
  renderer.End();
}

// Defined below.
HRESULT __stdcall PresentHook(IDXGISwapChain*, UINT, UINT);
HRESULT __stdcall Present1Hook(IDXGISwapChain*, UINT, UINT, const void*);
HRESULT __stdcall ResizeBuffersHook(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
void ApplyTestOverrides();
void ServiceGameFrame();

constexpr uintptr_t kMenuInputLockCountVA = 0x01CAD7FC;

DWORD WINAPI XInputGetStateHook(DWORD user_index, XINPUT_STATE* state) {
  if (!o_XInputGetState) return ERROR_DEVICE_NOT_CONNECTED;
  const DWORD result = o_XInputGetState(user_index, state);
  if (result == ERROR_SUCCESS && state && ThePanel().game_loaded() &&
      !ThePanel().menu_input_locked()) {
    state->Gamepad.wButtons &= static_cast<WORD>(~XINPUT_GAMEPAD_BACK);
  }
  return result;
}

bool InstallSelectSuppression() {
  void** slot = reinterpret_cast<void**>(Rebase(kXInputGetStatePointerVA));
  void* resolved = nullptr;
  if (!Read(slot, &resolved) || !Executable(resolved, 1)) {
    DXS_WARN("[input] game XInputGetState pointer is not ready; Select remains vanilla");
    return false;
  }
  o_XInputGetState = reinterpret_cast<XInputGetStateFn>(resolved);
  if (!g_hooks.Add(slot, 0, reinterpret_cast<void*>(&XInputGetStateHook))) {
    o_XInputGetState = nullptr;
    DXS_WARN("[input] could not hook the verified XInputGetState slot");
    return false;
  }
  DXS_INFO("[input] Select/Back is reserved for the custom lower-screen pages during gameplay");
  return true;
}

void** VTableOf(void* object) {
  void* vt = nullptr;
  return Read(object, &vt) ? static_cast<void**>(vt) : nullptr;
}

// Patches Present, Present1 and ResizeBuffers on whatever vtable this swapchain
// carries.
//
// Called for every swapchain seen, not just the first, because DXGI does not
// guarantee that two swapchains share a vtable: a legacy CreateSwapChain and a
// CreateSwapChainForHwnd can come back as different concrete types, and the
// probe that reads the vtable at startup uses the legacy path. Adopting each
// one as it appears is what stops a game that took the other route from
// presenting through an unpatched slot. Already-hooked vtables are skipped, so
// calling this repeatedly is free.
bool AdoptSwapChainVTable(IDXGISwapChain* chain, const char* whose) {
  void** vt = VTableOf(chain);
  if (!vt) return false;

  bool p = g_hooks.Original(vt, kSwapChainPresent) != nullptr;
  if (!p) {
    void* present = nullptr;
    if (Read(vt + kSwapChainPresent, &present) && Executable(present, 1)) {
      if (!o_Present) o_Present = reinterpret_cast<PresentFn>(present);
      p = g_hooks.Add(vt, kSwapChainPresent, reinterpret_cast<void*>(&PresentHook));
    }
  }

  bool r = g_hooks.Original(vt, kSwapChainResizeBuffers) != nullptr;
  if (!r) {
    void* resize = nullptr;
    if (Read(vt + kSwapChainResizeBuffers, &resize) && Executable(resize, 1)) {
      if (!o_ResizeBuffers) o_ResizeBuffers = reinterpret_cast<ResizeFn>(resize);
      r = g_hooks.Add(vt, kSwapChainResizeBuffers,
                      reinterpret_cast<void*>(&ResizeBuffersHook));
    }
  }

  // Slot 22 exists only when QueryInterface says this object implements
  // IDXGISwapChain1. Never probe past an IDXGISwapChain vtable based only on a
  // readable adjacent pointer.
  bool p1 = false;
  IDXGISwapChain1* chain1 = nullptr;
  if (SUCCEEDED(chain->QueryInterface(IID_IDXGISwapChain1,
                                      reinterpret_cast<void**>(&chain1))) && chain1) {
    void** vt1 = VTableOf(chain1);
    if (vt1) {
      p1 = g_hooks.Original(vt1, kSwapChainPresent1) != nullptr;
      if (!p1) {
        void* present1 = nullptr;
        if (Read(vt1 + kSwapChainPresent1, &present1) && Executable(present1, 1)) {
          if (!o_Present1) o_Present1 = reinterpret_cast<Present1Fn>(present1);
          p1 = g_hooks.Add(vt1, kSwapChainPresent1,
                           reinterpret_cast<void*>(&Present1Hook));
        }
      }
    }
    chain1->Release();
  }
  DXS_INFO("[d3d] adopted %s swapchain vtable %p: Present=%s Present1=%s Resize=%s",
           whose, static_cast<void*>(vt), p ? "yes" : "no", p1 ? "yes" : "no",
           r ? "yes" : "no");
  return p && r;
}

PresentFn OriginalPresent(IDXGISwapChain* chain) {
  void** vt = VTableOf(chain);
  void* original = vt ? g_hooks.Original(vt, kSwapChainPresent) : nullptr;
  return original ? reinterpret_cast<PresentFn>(original) : o_Present;
}

Present1Fn OriginalPresent1(IDXGISwapChain* chain) {
  void** vt = VTableOf(chain);
  void* original = vt ? g_hooks.Original(vt, kSwapChainPresent1) : nullptr;
  return original ? reinterpret_cast<Present1Fn>(original) : o_Present1;
}

ResizeFn OriginalResize(IDXGISwapChain* chain) {
  void** vt = VTableOf(chain);
  void* original = vt ? g_hooks.Original(vt, kSwapChainResizeBuffers) : nullptr;
  return original ? reinterpret_cast<ResizeFn>(original) : o_ResizeBuffers;
}

std::string NameOfObject(const void* object) {
  const char* n = g_rtti.ClassOf(object);
  if (!n) return "<unknown>";
  return n[0] == '.' ? Demangle(n) : std::string(n);
}

// The object Scaleform's output routes through. Named here because static
// analysis could get as far as "a secondary base of the render device, written
// as object+0x10 after a 0x111770-byte allocation" and no further; RTTI settles
// it in one read.
void ReportRenderGlobal() {
  auto* slot = reinterpret_cast<void**>(Rebase(kRenderGlobalVA));
  void* object = nullptr;
  if (!Read(slot, &object) || !object) {
    DXS_WARN("[eng] the render global at 0x%08X is still null",
             static_cast<unsigned>(kRenderGlobalVA));
    return;
  }
  void** vt = VTableOf(object);
  DXS_INFO("[eng] render global -> %p, vtable %p, %u slots", object,
           static_cast<void*>(vt), VTableLength(vt));
  const char* by_vt = g_rtti.ClassOfVTable(vt);
  DXS_INFO("[eng]   class by vtable : %s", by_vt ? by_vt : "<not indexed>");
  DXS_INFO("[eng]   class by locator: %s", NameOfObject(object).c_str());
  auto* primary = static_cast<uint8_t*>(object) - 0x10;
  if (Readable(primary, sizeof(void*))) {
    DXS_INFO("[eng]   object-0x10     : %s", NameOfObject(primary).c_str());
  }
}

bool ReportBuild() {
  DXS_INFO("[mod] module base %p, size 0x%X, ASLR slide 0x%X",
           reinterpret_cast<void*>(ModuleBase()), static_cast<unsigned>(ModuleSize()),
           static_cast<unsigned>(ModuleBase() - kPreferredBase));

  struct Check { const char* cls; uint32_t slot; uintptr_t expect; const char* what; };
  const Check checks[] = {
      {"ScaleformMovieInstance", 12, kPlacementVA, "placement"},
      {"ScaleformRenderer", 12, kBeginDisplayVA, "BeginDisplay"},
      {"ScaleformRenderer", 4, kApplyVA, "apply output"},
  };
  bool matched = true;
  for (const Check& c : checks) {
    const RttiClass* e = g_rtti.Find(c.cls);
    if (!e) { DXS_ERR("[mod] %s missing from RTTI", c.cls); matched = false; continue; }
    void* fn = nullptr;
    Read(e->vtable + c.slot, &fn);
    const uintptr_t preferred = InModule(reinterpret_cast<uintptr_t>(fn))
        ? Unbase(reinterpret_cast<uintptr_t>(fn)) : 0;
    const bool ok = preferred == c.expect;
    if (!ok) matched = false;
    DXS_INFO("[mod] %s[%u] = 0x%08X  expected 0x%08X  %s  (%s)", c.cls, c.slot,
             static_cast<unsigned>(preferred), static_cast<unsigned>(c.expect),
             ok ? "match" : "DIFFERS", c.what);
  }
  if (!matched) {
    DXS_ERR("[mod] this executable does not match the supported 2.0.66.0 build;"
            " refusing to patch build-specific Scaleform slots");
  }

  unsigned controllers = 0;
  for (const RttiClass& c : g_rtti.classes()) {
    if (c.name.find("MovieController") != std::string::npos &&
        c.name.find("_ScriptType") == std::string::npos) ++controllers;
  }
  DXS_INFO("[mod] %u classes indexed, %u of them movie controllers",
           static_cast<unsigned>(g_rtti.size()), controllers);
  return matched;
}

void ReportDisplay() {
  DEVMODEW dm;
  ZeroMemory(&dm, sizeof(dm));
  dm.dmSize = sizeof(dm);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
    DXS_INFO("[disp] desktop %lux%lu @ %luHz", dm.dmPelsWidth, dm.dmPelsHeight,
             dm.dmDisplayFrequency);
  }
  if (g_game_window) {
    RECT wr, cr;
    if (GetWindowRect(g_game_window, &wr) && GetClientRect(g_game_window, &cr)) {
      const int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
      DXS_INFO("[disp] game window %ldx%ld at (%ld,%ld), client %dx%d, aspect %.4f",
               wr.right - wr.left, wr.bottom - wr.top, wr.left, wr.top, cw, ch,
               ch > 0 ? static_cast<double>(cw) / ch : 0.0);
    }
  }
  BOOL fullscreen = FALSE;
  if (g_swapchain && SUCCEEDED(g_swapchain->GetFullscreenState(&fullscreen, nullptr))) {
    DXS_INFO("[disp] swapchain is %s", fullscreen ? "EXCLUSIVE fullscreen" : "windowed");
  }
}

// The factory the device actually belongs to.
//
// Not necessarily the one CreateDXGIFactory handed the game: a device made with
// a null adapter gets an adapter chain DXGI built for itself, and DXGI is
// unreliable when a swapchain is created from a factory that is not the
// device's own. Walking device -> IDXGIDevice -> adapter -> parent is the only
// way to be sure the panel's surface comes from the right place.
IDXGIFactory* FactoryOfDevice(ID3D11Device* device) {
  if (!device) return nullptr;
  IDXGIDevice* dxgi_device = nullptr;
  if (FAILED(device->QueryInterface(IID_IDXGIDevice,
                                    reinterpret_cast<void**>(&dxgi_device))) || !dxgi_device) {
    return nullptr;
  }
  IDXGIAdapter* adapter = nullptr;
  IDXGIFactory* factory = nullptr;
  if (SUCCEEDED(dxgi_device->GetAdapter(&adapter)) && adapter) {
    adapter->GetParent(IID_IDXGIFactory, reinterpret_cast<void**>(&factory));
    adapter->Release();
  }
  dxgi_device->Release();
  return factory;
}

void Start() {
  if (!g_swapchain) return;
  HRESULT device_hr =
      g_swapchain->GetDevice(IID_ID3D11Device, reinterpret_cast<void**>(&g_device));
  if (g_device) g_device->GetImmediateContext(&g_context);
  if (FAILED(device_hr) || !g_device || !g_context) {
    DXS_ERR("[d3d] the selected swapchain has no usable D3D11 device/context; standing down");
    return;
  }

  if (IDXGIFactory* own = FactoryOfDevice(g_device)) {
    if (own != g_factory) {
      DXS_INFO("[d3d] the device's own factory is %p, not the %p we were handed"
               " -- using the device's", static_cast<void*>(own),
               static_cast<void*>(g_factory));
      if (g_factory) g_factory->Release();
      g_factory = own;
    } else {
      own->Release();
    }
  }
  // o_CreateSwapChain is deliberately NOT re-read from that factory's vtable.
  //
  // Every IDXGIFactory in the process shares one vtable, and this one is
  // already hooked -- so reading slot 10 back returns our own hook, and the
  // hook then calls itself. That is a stack overflow inside the first frame,
  // which is exactly how this crashed: two different factory objects, one
  // shared vtable, and an "original" that was not original at all. The value
  // captured when the hook was installed is the true original and is correct
  // for any factory sharing that vtable.
  DXGI_SWAP_CHAIN_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  if (SUCCEEDED(g_swapchain->GetDesc(&desc))) {
    g_game_window = desc.OutputWindow;
  }

  ApplyTestOverrides();

  // ResizeTarget/WM_SIZE may have changed both the window and the buffers.
  // Read the live description after the desktop overrides instead of logging
  // the stale creation values.
  ZeroMemory(&desc, sizeof(desc));
  if (SUCCEEDED(g_swapchain->GetDesc(&desc))) {
    g_game_window = desc.OutputWindow;
    DXS_INFO("[d3d] game swapchain %ux%u fmt=%d windowed=%d buffers=%u",
             desc.BufferDesc.Width, desc.BufferDesc.Height,
             static_cast<int>(desc.BufferDesc.Format), desc.Windowed, desc.BufferCount);
  }
  ReportDisplay();

  if (!g_rtti.Build()) {
    DXS_ERR("[mod] RTTI scan failed -- standing down, the game is unaffected");
    return;
  }
  if (!ReportBuild()) return;
  ReportRenderGlobal();

  if (!g_factory) {
    DXS_ERR("[d3d] no factory is available for the panel swapchain; standing down");
    return;
  }

  Panel& panel = ThePanel();
  if (!panel.Create(g_factory, g_device, g_context,
                    reinterpret_cast<Panel::CreateSwapChainFn>(o_CreateSwapChain))) {
    DXS_ERR("[mod] no panel -- standing down, the game is unaffected");
    return;
  }
  TheLayout().Build(panel.width(), panel.height());
  TheLayout().LogSummary();

  if (!TheBridge().Attach(&g_rtti, &g_hooks, g_context)) {
    DXS_ERR("[mod] could not attach every required Scaleform/D3D seam; removing the panel");
    panel.Destroy();
    return;
  }
  // This experimental native hook calls undocumented energy-system methods.
  // Keep it opt-in until their ABI is proven on Wine/WEMU as well as Windows;
  // installing it during startup can otherwise crash on the first gameplay
  // tick immediately after a save finishes loading.
  if (Cfg().GetBool("gameplay.full_energy_recharge", false)) {
    if (!InstallGameplayEnhancements(&g_hooks)) {
      DXS_WARN("[gameplay] optional full-reserve energy recharge is unavailable");
    }
  } else {
    DXS_INFO("[gameplay] full-reserve energy recharge disabled (safe default)");
  }
  if (!Cfg().GetBool("hud.custom_crosshair", false)) {
    DXS_INFO("[hud] custom upper-screen crosshair disabled (safe default)");
  }
  if (Cfg().GetBool("input.reserve_back_experimental", false)) {
    InstallSelectSuppression();
  } else {
    DXS_INFO("[input] controller Back/Select remains assigned to normal gameplay");
  }
  if (!TheSmartGlass().Install(&g_hooks)) {
    DXS_WARN("[sgui] native data bridge unavailable; inventory fallback remains active");
  }
  if (!InstallNativeRadarCapture(&g_rtti, &g_hooks)) {
    DXS_WARN("[map] live radar contacts unavailable; map geometry/player data remain active");
  }
  if (!StartTouchInput()) {
    DXS_WARN("[mod] lower-panel touch input failed to start; visual output remains active");
  }
  g_ready.store(true);
  DXS_INFO("[mod] up. Independent lower panel is active; the retail upper HUD"
           " remains untouched unless experimental suppression was enabled.");
}

bool LooksLikeGameSwapChain(IDXGISwapChain* chain) {
  if (!chain || chain == ThePanel().swapchain()) return false;
  DXGI_SWAP_CHAIN_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  if (FAILED(chain->GetDesc(&desc)) || !desc.OutputWindow || !IsWindow(desc.OutputWindow)) {
    return false;
  }

  DWORD window_process = 0;
  GetWindowThreadProcessId(desc.OutputWindow, &window_process);
  if (window_process != GetCurrentProcessId()) return false;

  RECT client = {};
  if (!GetClientRect(desc.OutputWindow, &client)) return false;
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width < 320 || height < 200) return false;  // filters probe/overlay surfaces

  ID3D11Device* device = nullptr;
  const HRESULT hr = chain->GetDevice(IID_ID3D11Device,
                                      reinterpret_cast<void**>(&device));
  if (device) device->Release();
  return SUCCEEDED(hr) && device != nullptr;
}

bool SelectGameSwapChain(IDXGISwapChain* self, const char* present_kind) {
  bool selected = false;
  bool newly_selected = false;
  AcquireSRWLockExclusive(&g_frame_lock);
  if (g_swapchain) {
    selected = self == g_swapchain;
  } else if (LooksLikeGameSwapChain(self)) {
    // Own the reference and raw pointer before publishing started. The old
    // order exposed g_started while g_swapchain was still null/unowned.
    self->AddRef();
    g_swapchain = self;
    g_service_thread.store(GetCurrentThreadId(), std::memory_order_release);
    g_service_thread_last_present_ms.store(GetTickCount64(),
                                           std::memory_order_release);
    g_started.store(true, std::memory_order_release);
    selected = true;
    newly_selected = true;
  }
  ReleaseSRWLockExclusive(&g_frame_lock);
  if (newly_selected) {
    DXS_INFO("[d3d] selected the game swapchain %p through %s on owner thread %lu",
             static_cast<void*>(self), present_kind,
             static_cast<unsigned long>(
                 g_service_thread.load(std::memory_order_acquire)));
  }
  return selected;
}

bool ClaimServiceThread() {
  const DWORD current = GetCurrentThreadId();
  DWORD owner = g_service_thread.load(std::memory_order_acquire);
  const uint64_t now = GetTickCount64();
  if (owner == current) {
    if (g_panel_rehome_required.load(std::memory_order_acquire)) {
      const uint64_t retry =
          g_panel_rehome_retry_ms.load(std::memory_order_acquire);
      if (retry != 0 && now < retry) return false;
      bool rehomed = false;
      // Never hold up the top-screen Present while a retired owner is still
      // unwinding service work. A later frame retries the migration.
      if (!TryAcquireSRWLockExclusive(&g_frame_lock)) return false;
      if (!g_ready.load(std::memory_order_acquire)) {
        rehomed = true;
      } else {
        rehomed = ThePanel().RehomeToCurrentThread();
        if (rehomed) {
          TheLayout().Build(ThePanel().width(), ThePanel().height());
          g_display_change_pending.store(false, std::memory_order_release);
        }
      }
      ReleaseSRWLockExclusive(&g_frame_lock);
      if (!rehomed) {
        g_panel_rehome_retry_ms.store(now + 1000,
                                      std::memory_order_release);
        return false;
      }
      g_panel_rehome_retry_ms.store(0, std::memory_order_release);
      g_panel_rehome_required.store(false, std::memory_order_release);
    }
    g_service_thread_last_present_ms.store(now, std::memory_order_release);
    return true;
  }
  const uint64_t last_present =
      g_service_thread_last_present_ms.load(std::memory_order_acquire);
  // DXVK may retire and replace its Present worker after a renderer reset. A
  // two-second quiet period distinguishes that from genuinely concurrent
  // overlay/driver Presents. g_frame_lock still serializes the D3D immediate
  // context and all Panel state after ownership transfers.
  if (owner != 0 && last_present != 0 && now - last_present < 2000) {
    return false;
  }
  if (!g_service_thread.compare_exchange_strong(owner, current,
                                                 std::memory_order_acq_rel)) {
    return owner == current;
  }
  g_panel_rehome_required.store(g_ready.load(std::memory_order_acquire),
                                std::memory_order_release);
  g_panel_rehome_retry_ms.store(0, std::memory_order_release);
  g_service_thread_last_present_ms.store(now, std::memory_order_release);
  DXS_INFO("[d3d] primary Present service migrated from thread %lu to %lu",
           static_cast<unsigned long>(owner),
           static_cast<unsigned long>(current));
  return ClaimServiceThread();
}

// Starts the auxiliary output only after DXVK has completed one presentation
// from the game's primary swapchain. Creating the panel swapchain sooner is
// harmless on Windows but deadlocks DXVK's WSI startup on the Thor: both sets
// of buffers are imported, yet neither window submits a Present request.
//
// The first frame after Start is intentionally not serviced. Start installs
// the Scaleform hooks after that frame was already rendered, so presenting the
// panel then would only show an empty clear and needlessly compete with the
// primary swapchain during the same initialization turn.
void StartAfterPrimaryPresent() {
  bool expected = false;
  if (!g_initialization_attempted.compare_exchange_strong(expected, true)) {
    AcquireSRWLockExclusive(&g_frame_lock);
    ServiceGameFrame();
    ReleaseSRWLockExclusive(&g_frame_lock);
    return;
  }
  DXS_INFO("[d3d] primary swapchain presented successfully; creating the panel now");
  // Start can synchronously call the hooked ResizeBuffers path while applying
  // a desktop-test resolution. Do not hold g_frame_lock across initialization;
  // readiness remains false, so every competing service attempt is inert.
  Start();
}

void ServiceGameFrame() {
  const uint32_t frame = g_frame.fetch_add(1, std::memory_order_relaxed);
  if (!g_ready.load(std::memory_order_acquire)) return;
  if (g_display_change_pending.exchange(false, std::memory_order_acq_rel)) {
    ThePanel().OnDisplayChanged();
    TheLayout().Build(ThePanel().width(), ThePanel().height());
  }
  int menu_input_locks = 0;
  if (Read(reinterpret_cast<const void*>(Rebase(kMenuInputLockCountVA)),
           &menu_input_locks)) {
    ThePanel().ObserveGameState(menu_input_locks > 0);
    ThePanel().ObserveMenuInputState(menu_input_locks > 0);
  }
  DrainGameActions();
  TheSmartGlass().SetPage(ThePanel().active_tab());
  TheSmartGlass().Tick(frame, ThePanel().game_loaded());
  ThePanel().Present();
  TheBridge().BeginFrame(frame);
  if (frame == 300 || frame == 1800) TheBridge().LogReport();
}

thread_local bool g_inside_present = false;

HRESULT __stdcall PresentHook(IDXGISwapChain* self, UINT interval, UINT flags) {
  PresentFn original = OriginalPresent(self);
  if (!original) return E_FAIL;
  if (g_inside_present || self == ThePanel().swapchain()) {
    return original(self, interval, flags);
  }

  g_inside_present = true;
  const bool game_chain = SelectGameSwapChain(self, "Present");
  const bool service_owner = game_chain && ClaimServiceThread();

  if (service_owner) {
    AcquireSRWLockExclusive(&g_frame_lock);
    DrawPreservedCrosshair(self);
    ReleaseSRWLockExclusive(&g_frame_lock);
  }

  // Let the game's swapchain become WSI's primary presentation stream before
  // the auxiliary panel submits anything.  Native Windows accepts either
  // order, but DXVK/Wine can initialise its Present worker from the first
  // swapchain submitted.  Presenting the freshly-created panel first therefore
  // left both chains queued behind an auxiliary off-screen surface on WEMU:
  // the panel call returned, then the game's first real Present never reached
  // the X server.  The symptom was a working PC build and a Thor session with
  // four panel buffers imported, four game buffers imported, and zero frames
  // from either window.  Game-first ordering also guarantees that a slow or
  // temporarily occluded panel can never prevent the top screen's current
  // frame from being handed to the compositor.
  const HRESULT hr = original(self, interval, flags);
  if (game_chain && SUCCEEDED(hr) && ClaimServiceThread()) {
    StartAfterPrimaryPresent();
  }
  g_inside_present = false;
  return hr;
}

// The DXGI 1.2 present. A swapchain made with CreateSwapChainForHwnd may be
// presented through this instead of slot 8, in which case a hook on slot 8
// alone never sees a frame -- which is exactly the symptom the log showed.
HRESULT __stdcall Present1Hook(IDXGISwapChain* self, UINT interval, UINT flags,
                               const void* parameters) {
  Present1Fn original = OriginalPresent1(self);
  if (!original) return E_FAIL;
  if (g_inside_present || self == ThePanel().swapchain()) {
    return original(self, interval, flags, parameters);
  }

  g_inside_present = true;
  const bool game_chain = SelectGameSwapChain(self, "Present1");
  const bool service_owner = game_chain && ClaimServiceThread();
  if (service_owner) {
    AcquireSRWLockExclusive(&g_frame_lock);
    DrawPreservedCrosshair(self);
    ReleaseSRWLockExclusive(&g_frame_lock);
  }
  const HRESULT hr = original(self, interval, flags, parameters);
  if (game_chain && SUCCEEDED(hr) && ClaimServiceThread()) {
    StartAfterPrimaryPresent();
  }
  g_inside_present = false;
  return hr;
}

HRESULT __stdcall ResizeBuffersHook(IDXGISwapChain* self, UINT count, UINT width,
                                    UINT height, DXGI_FORMAT format, UINT flags) {
  ResizeFn original = OriginalResize(self);
  if (!original) return E_FAIL;
  AcquireSRWLockExclusive(&g_frame_lock);
  const bool game_chain = self == g_swapchain;
  if (game_chain) DXS_INFO("[d3d] the game resized its swapchain to %ux%u", width, height);
  if (game_chain) ReleaseGameSurface();
  ReleaseSRWLockExclusive(&g_frame_lock);
  HRESULT hr = original(self, count, width, height, format, flags);
  // The Thor's panel does not change size when the game's resolution does, but
  // a desktop with a monitor plugged in mid-session does, and a stale swapchain
  // there is a dead surface rather than a wrong one.
  if (game_chain && SUCCEEDED(hr) && g_ready.load(std::memory_order_acquire)) {
    // Window and panel swapchain lifecycle stays on the original service
    // thread. The next successful primary Present applies this request.
    g_display_change_pending.store(true, std::memory_order_release);
  }
  return hr;
}

// Desktop testing only.
//
// On a development machine there is no second monitor to put the panel on, and
// an exclusive-fullscreen game covers whatever window is created for it, so
// there is nothing to look at. Wine compatibility is handled in the factory
// hooks before the game swapchain exists. Changing DXGI fullscreen state from
// inside the first Present callback tears down DXVK's WSI surface while WEMU is
// latching it, which can leave the top panel permanently white.
void ApplyTestOverrides() {
  if (!g_swapchain) return;
  const bool requested_for_test = Cfg().GetBool("test.windowed", false);
  const bool borderless_top = Cfg().GetBool("display.borderless_16_9", true);
  if (!requested_for_test && !borderless_top) return;

  BOOL fullscreen = FALSE;
  if (SUCCEEDED(g_swapchain->GetFullscreenState(&fullscreen, nullptr)) && fullscreen) {
    HRESULT hr = g_swapchain->SetFullscreenState(FALSE, nullptr);
    DXS_INFO("[test] took the game out of exclusive fullscreen -> 0x%08lX",
             static_cast<unsigned long>(hr));
  } else {
    DXS_INFO("[test] the game was already windowed");
  }

  const int requested_width = borderless_top
      ? Cfg().GetInt("display.width", 1280)
      : Cfg().GetInt("test.width", 0);
  const int requested_height = borderless_top
      ? Cfg().GetInt("display.height", 720)
      : Cfg().GetInt("test.height", 0);
  if (requested_width <= 0 || requested_height <= 0) return;

  if (borderless_top && g_game_window) {
    // DXHR remembers 1920x1080 as its full-screen target but can still create
    // a bordered 640x480 Wine window. WEMU then correctly preserves that 4:3
    // aspect and the result is a small square in the top panel. Give the game
    // an exact borderless 16:9 client instead; WM_SIZE remains the owner of the
    // subsequent ResizeBuffers operation.
    const LONG_PTR old_style = GetWindowLongPtrW(g_game_window, GWL_STYLE);
    const LONG_PTR border_bits = WS_CAPTION | WS_THICKFRAME | WS_BORDER |
                                 WS_DLGFRAME | WS_SYSMENU | WS_MINIMIZEBOX |
                                 WS_MAXIMIZEBOX;
    const LONG_PTR new_style = (old_style & ~border_bits) | WS_POPUP | WS_VISIBLE;
    SetWindowLongPtrW(g_game_window, GWL_STYLE, new_style);
    SetWindowPos(g_game_window, nullptr, 0, 0, requested_width, requested_height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    DXS_INFO("[display] borderless top client requested at %dx%d", requested_width,
             requested_height);
  }

  DXGI_SWAP_CHAIN_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  if (FAILED(g_swapchain->GetDesc(&desc))) {
    DXS_WARN("[test] could not read the swapchain before the requested resize");
    return;
  }

  // ResizeTarget is the DXGI-supported way to resize a windowed target. It
  // raises WM_SIZE, allowing the game to release its own back-buffer views and
  // call ResizeBuffers safely; forcing ResizeBuffers here would fail while the
  // game still owns those references.
  DXGI_MODE_DESC mode = desc.BufferDesc;
  mode.Width = static_cast<UINT>(requested_width);
  mode.Height = static_cast<UINT>(requested_height);
  HRESULT hr = g_swapchain->ResizeTarget(&mode);
  DXS_INFO("[display] requested a %dx%d game client through ResizeTarget -> 0x%08lX",
           requested_width, requested_height, static_cast<unsigned long>(hr));

  // Some compatibility layers accept ResizeTarget but do not resize a normal
  // Win32 window. Fall back to an exact client-area resize without touching the
  // back buffers; the game's WM_SIZE path still owns that operation.
  bool needs_window_fallback = FAILED(hr);
  if (g_game_window) {
    RECT client = {};
    if (!GetClientRect(g_game_window, &client) ||
        client.right - client.left != requested_width ||
        client.bottom - client.top != requested_height) {
      needs_window_fallback = true;
    }
  }
  if (needs_window_fallback && g_game_window) {
    RECT outer = {0, 0, requested_width, requested_height};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(g_game_window, GWL_STYLE));
    const DWORD ex_style = static_cast<DWORD>(GetWindowLongPtrW(g_game_window, GWL_EXSTYLE));
    AdjustWindowRectEx(&outer, style, FALSE, ex_style);
    SetWindowPos(g_game_window, nullptr, 0, 0, outer.right - outer.left,
                 outer.bottom - outer.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    DXS_INFO("[test] resized the game window to an exact %dx%d client fallback",
             requested_width, requested_height);
  }
}

}  // namespace

// Hooking Present without owning the swapchain that will call it.
//
// The obvious route -- hook CreateSwapChain on the factory the game asked us
// for -- does not work here, and the first run proved it: the log stopped after
// two lines because Present never arrived. The game creates its device with a
// null adapter, so DXGI builds an adapter chain of its own, and the factory the
// swapchain is actually made from is reached through
// device -> IDXGIDevice -> GetAdapter -> GetParent. That is DXGI's internal
// factory, not the one CreateDXGIFactory handed back, so a hook on ours is on
// an object the game never uses for this.
//
// Every IDXGISwapChain in a process shares one vtable, because there is one
// DXGI implementation behind all of them. So: make a throwaway device and
// swapchain on a hidden 1x1 window, patch slot 8 and slot 13 of the vtable it
// comes back with, and release it. The game's swapchain, whenever and however
// it is created, is already hooked. This is what every D3D11 overlay does, and
// it is indifferent to whether the game ends up calling CreateSwapChain or
// CreateSwapChainForHwnd.
bool HookSwapChainVTable() {
  HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
  if (!d3d11) {
    DXS_ERR("[d3d] d3d11.dll is not loaded yet; cannot place the Present hook");
    return false;
  }
  using CreateDeviceAndSwapChainFn = HRESULT(__stdcall*)(
      IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
      const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*,
      ID3D11DeviceContext**);
  auto create = reinterpret_cast<CreateDeviceAndSwapChainFn>(
      GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain"));
  if (!create) {
    DXS_ERR("[d3d] no D3D11CreateDeviceAndSwapChain in d3d11.dll");
    return false;
  }

  WNDCLASSEXW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"DXHRSecondScreenProbe";
  RegisterClassExW(&wc);
  HWND probe_window = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED,
                                      0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
  if (!probe_window) {
    DXS_ERR("[d3d] could not create the probe window (%lu)", GetLastError());
    return false;
  }

  DXGI_SWAP_CHAIN_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.BufferDesc.Width = 1;
  desc.BufferDesc.Height = 1;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 1;
  desc.OutputWindow = probe_window;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  IDXGISwapChain* chain = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                      D3D_FEATURE_LEVEL_10_0};
  HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 3,
                      D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
  if (FAILED(hr) || !chain) {
    DXS_ERR("[d3d] the probe device failed 0x%08lX", static_cast<unsigned long>(hr));
    DestroyWindow(probe_window);
    return false;
  }

  const bool ok = AdoptSwapChainVTable(chain, "probe");
  if (!ok) DXS_ERR("[d3d] could not patch the required swapchain slots");

  // The throwaway objects go; the vtable patch stays, because the vtable is
  // shared and outlives any one swapchain.
  if (context) context->Release();
  if (device) device->Release();
  if (chain) chain->Release();
  DestroyWindow(probe_window);
  return ok;
}

// Both creation routes, hooked on the factory vtable.
//
// All IDXGIFactory objects from one dxgi.dll share a vtable -- proved by the
// probe device, whose internal factory arrived at our hook -- so patching the
// one the game asked us for also patches the internal one it really uses. What
// the game does *not* do is call slot 10: the previous run installed that hook
// and never saw the game's swapchain, so the remaining route is slot 15.
HRESULT __stdcall CreateSwapChainHook(IDXGIFactory* self, IUnknown* device,
                                      DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out) {
  // Refuses to call itself. One shared vtable across every factory object makes
  // "read the original back out of the vtable" a way to capture the hook, and
  // the result is a stack overflow in the first frame rather than anything that
  // looks like a bad pointer. Cheap to check, and the check is the difference
  // between a logged error and a crash dump.
  if (reinterpret_cast<void*>(o_CreateSwapChain) ==
      reinterpret_cast<void*>(&CreateSwapChainHook)) {
    DXS_ERR("[d3d] CreateSwapChain original is the hook itself; refusing to recurse");
    return E_FAIL;
  }
  HRESULT hr = o_CreateSwapChain ? o_CreateSwapChain(self, device, desc, out) : E_FAIL;
  if (SUCCEEDED(hr) && out && *out) {
    if (desc) {
      DXS_INFO("[d3d] CreateSwapChain %ux%u windowed=%d hwnd=%p",
               desc->BufferDesc.Width, desc->BufferDesc.Height, desc->Windowed,
               static_cast<void*>(desc->OutputWindow));
    }
    // Not while the panel is being built: its surface is ours, not the game's.
    AdoptSwapChainVTable(*out, "CreateSwapChain");
  }
  return hr;
}

HRESULT __stdcall CreateForHwndHook(IDXGIFactory* self, IUnknown* device, HWND hwnd,
                                    const void* desc, const void* fullscreen,
                                    IDXGIOutput* restrict_to, IDXGISwapChain** out) {
  if (reinterpret_cast<void*>(o_CreateForHwnd) ==
      reinterpret_cast<void*>(&CreateForHwndHook)) {
    DXS_ERR("[d3d] CreateSwapChainForHwnd original is the hook itself; refusing");
    return E_FAIL;
  }
  HRESULT hr = o_CreateForHwnd
      ? o_CreateForHwnd(self, device, hwnd, desc, fullscreen, restrict_to, out)
      : E_FAIL;
  if (SUCCEEDED(hr) && out && *out) {
    DXS_INFO("[d3d] CreateSwapChainForHwnd hwnd=%p", static_cast<void*>(hwnd));
    AdoptSwapChainVTable(*out, "CreateSwapChainForHwnd");
  }
  return hr;
}

// Turns "nothing happened" into a statement.
//
// Every failure so far has looked identical from outside: the game runs, the
// panel is absent, and the log simply stops. That is the least useful shape a
// problem can have, so if no frame has arrived by the time the game is
// certainly rendering, the mod says so and says what it did hook.
// Which graphics library the process actually ended up using.
//
// Worth asking from in here rather than from outside: this is a 32-bit process,
// and a 64-bit tool enumerating its modules gets the WOW64 view, which lists
// ntdll and little else. GetModuleHandle from inside is authoritative.
//
// It matters because the executable carries both renderers -- Direct3DCreate9
// and d3d9.dll are in the string table beside D3D11CreateDevice -- and if the
// game settled on D3D9 then there is no DXGI swapchain to hook at all, and no
// amount of work on the DXGI side will ever see a frame.
void ReportGraphicsModules(const char* tag) {
  static const wchar_t* kNames[] = {L"d3d9.dll", L"d3d11.dll", L"dxgi.dll",
                                    L"d3d10.dll", L"opengl32.dll"};
  for (const wchar_t* name : kNames) {
    HMODULE m = GetModuleHandleW(name);
    char narrow[64];
    WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr);
    if (!m) { DXS_INFO("[%s] %-14s not loaded", tag, narrow); continue; }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(m, path, MAX_PATH);
    char wide_path[MAX_PATH * 2];
    WideCharToMultiByte(CP_ACP, 0, path, -1, wide_path, sizeof(wide_path), nullptr, nullptr);
    DXS_INFO("[%s] %-14s loaded at %p  %s", tag, narrow, static_cast<void*>(m), wide_path);
  }
}

DWORD WINAPI WatchdogThread(LPVOID) {
  Sleep(25000);
  if (g_started.load()) return 0;
  DXS_ERR("[watchdog] 25s and not one frame has reached the mod.");
  DXS_ERR("[watchdog]   Present  hooked: %s", o_Present ? "yes" : "no");
  DXS_ERR("[watchdog]   Present1 hooked: %s", o_Present1 ? "yes" : "no");
  DXS_ERR("[watchdog]   CreateSwapChain        hooked: %s",
          o_CreateSwapChain ? "yes" : "no");
  DXS_ERR("[watchdog]   CreateSwapChainForHwnd hooked: %s",
          o_CreateForHwnd ? "yes" : "no");
  ReportGraphicsModules("watchdog");

  if (GetModuleHandleW(L"d3d9.dll")) {
    DXS_ERR("[watchdog] d3d9.dll is loaded. This build carries both renderers and"
            " has chosen Direct3D 9, so there is no DXGI swapchain in this process"
            " and nothing on the DXGI side can ever see a frame. Either switch the"
            " game to its DirectX 11 renderer, or the mod needs to hook"
            " IDirect3DDevice9::Present instead.");
  } else if (!GetModuleHandleW(L"d3d11.dll")) {
    DXS_ERR("[watchdog] neither d3d9 nor d3d11 is loaded; the game has not created"
            " a renderer at all yet, so it is still loading or stuck before it.");
  }
  return 0;
}

void ProbeStage() {}

void ProbeOnFactory(IUnknown* factory) {
  if (g_factory_seen.exchange(true)) return;

  LogOpen(L"DeusExSecondScreen.log");
  Cfg().Load();
  DXS_INFO("Deus Ex: Human Revolution -- second screen");
  DXS_INFO("[mod] build %s %s", __DATE__, __TIME__);
  DXS_INFO("Reads DeusExSecondScreen.ini beside the executable. Delete dxgi.dll to remove.");
  if (!Cfg().GetBool("panel.enabled", true)) {
    DXS_INFO("[mod] panel.enabled=0; DXGI is forwarded untouched and no hooks are installed");
    return;
  }

  // Engine detours are process-lifetime. Pin the proxy before installing one
  // so an accidental FreeLibrary cannot unmap callback code while the game is
  // still rendering. Windows releases the module with the process.
  HMODULE pinned = nullptr;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
          reinterpret_cast<LPCWSTR>(&ProbeOnFactory), &pinned)) {
    DXS_ERR("[mod] could not pin the proxy module (%lu); refusing engine hooks",
            GetLastError());
    return;
  }
  DXS_INFO("[mod] proxy pinned for the lifetime of the game process");

  // Order matters, and getting it wrong cost a run. The probe below creates a
  // throwaway swapchain, and if this factory's CreateSwapChain were already
  // hooked that throwaway would arrive at our own hook, be mistaken for the
  // game's, and take the Present slot -- after which the real vtable patch
  // fails because the slot is already occupied. So the vtable is patched first,
  // while nothing of ours is in the way.
  if (!HookSwapChainVTable()) {
    DXS_ERR("[mod] without a Present hook nothing further can run; standing down");
    return;
  }

  IDXGIFactory* f = nullptr;
  if (SUCCEEDED(factory->QueryInterface(IID_IDXGIFactory, reinterpret_cast<void**>(&f))) && f) {
    g_factory = f;
    if (void** vt = VTableOf(f)) {
      Read(vt + kFactoryCreateSwapChain, reinterpret_cast<void**>(&o_CreateSwapChain));
      const bool legacy = g_hooks.Add(vt, kFactoryCreateSwapChain,
                                      reinterpret_cast<void*>(&CreateSwapChainHook));

      // Whether slot 15 exists is decided by QueryInterface, not by walking the
      // vtable. VTableLength answers 0 for anything outside the executable --
      // it tests against the game's own .text -- so using it here silently
      // skipped this hook, which is precisely the route the game turned out to
      // take. QueryInterface is the only honest test of what an interface is.
      bool for_hwnd = false;
      IDXGIFactory2* f2 = nullptr;
      if (SUCCEEDED(f->QueryInterface(IID_IDXGIFactory2,
                                      reinterpret_cast<void**>(&f2))) && f2) {
        // Same object, so the same vtable: the factory interfaces are one
        // single-inheritance chain and QI hands back the same pointer.
        Read(vt + kFactoryCreateSwapChainForHwnd, reinterpret_cast<void**>(&o_CreateForHwnd));
        for_hwnd = g_hooks.Add(vt, kFactoryCreateSwapChainForHwnd,
                               reinterpret_cast<void*>(&CreateForHwndHook));
        f2->Release();
      }
      DXS_INFO("[d3d] factory %p, vtable %p: CreateSwapChain=%s CreateSwapChainForHwnd=%s",
               static_cast<void*>(f), static_cast<void*>(vt),
               legacy ? "hooked" : "FAILED",
               for_hwnd ? "hooked" : (f2 ? "FAILED" : "not supported"));
    }
  } else {
    DXS_ERR("[d3d] what came back is not an IDXGIFactory");
  }

  ReportGraphicsModules("mod");
  if (HANDLE t = CreateThread(nullptr, 0, WatchdogThread, nullptr, 0, nullptr)) {
    CloseHandle(t);
  }
}

void ProbeShutdown() {
  const bool was_ready = g_ready.exchange(false);
  if (was_ready) TheBridge().LogReport();
  DisableNativeMapCapture();
  TheSmartGlass().Shutdown();
  TheBridge().Detach();
  StopTouchInput();
  ThePanel().Destroy();
  ReleaseGameSurface();
  g_hooks.RemoveAll();
  ResetNativeMapCapture();
  if (g_context) { g_context->Release(); g_context = nullptr; }
  if (g_device) { g_device->Release(); g_device = nullptr; }
  if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
  if (g_factory) { g_factory->Release(); g_factory = nullptr; }
  DXS_INFO("[mod] unloaded cleanly");
  LogClose();
}

}  // namespace dxs

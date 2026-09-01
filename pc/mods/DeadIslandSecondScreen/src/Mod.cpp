// Bringing the second screen up in Dead Island.
//
// Three things have to happen and they happen in this order, because each one
// only becomes possible once the previous has:
//
//  1. The proxy is loaded. rd3d11_x64_rwdi.dll imports d3d11 and dxgi
//     statically, so a dxgi.dll beside the executable is resolved in the system
//     one's place while the renderer module is being mapped.
//  2. The first Present. That is the earliest moment a device, a context and a
//     swapchain all exist, which is when the panel window and its own swapchain
//     can be created.
//  3. The main level appears. Dead Island keeps several ILevel instances alive
//     at once for the front end, movies and the playable world. gamedll asks
//     each one whether it is the main level, so that imported call is the safe
//     discriminator: only a pointer for which the game's own IsMainLevel()
//     returns true is published to the render thread. No code is patched -- it
//     is a pointer swap in a table the loader itself filled in.
//
// What this build does not yet do is move anything. It resolves the API,
// captures the level, walks the live UI tree and writes down what it found.
// That order is deliberate: the previous mod in this folder spent four runs
// failing because it built a routing design on top of an assumption about the
// UI's shape that was never checked against the running game.

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Config.h"
#include "Engine.h"
#include "Game.h"
#include "GameState.h"
#include "Hook.h"
#include "Log.h"
#include "Mem.h"
#include "NativeMap.h"
#include "NativeUi.h"
#include "Panel.h"
#include "Rtti.h"
#include "TouchInput.h"
#include "Ui.h"
#include "UiProbe.h"

namespace dis {
namespace {

constexpr uint32_t kSwapChainPresent = 8;
constexpr uint32_t kSwapChainResizeBuffers = 13;

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using IsMainLevelFn = bool (*)(void*);
using DeactivateLevelFn = void (*)(void*);
using GetUIManagerFn = void* (*)(void*);
using RegisterUIScreenFn = int (*)(void*, const void*, int, int, bool,
                                   const void*, int);
using OnRenderFn = void (*)(void*);

extern "C" {
extern uint64_t dis_ui_calls[64];
extern void* dis_ui_orig[64];
}

PresentFn o_Present = nullptr;
ResizeFn o_ResizeBuffers = nullptr;
IsMainLevelFn o_IsMainLevel = nullptr;
DeactivateLevelFn o_DeactivateLevel = nullptr;
GetUIManagerFn o_GetUIManager = nullptr;
RegisterUIScreenFn o_RegisterUIScreen = nullptr;

// Process-lifetime proxy hooks are intentionally leaked. Static HookSet
// destructors would restore vtables and take mutexes during CRT teardown under
// the loader lock—the same unsafe unload work deliberately removed from
// DllMain. Windows reclaims these allocations with the process.
HookSet& g_hooks = *new HookSet();
HookSet& g_probe_hooks = *new HookSet();
HookSet& g_trial_hooks = *new HookSet();
IDXGISwapChain* g_swapchain = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGIFactory* g_factory = nullptr;

std::atomic<bool> g_started{false};
std::atomic<bool> g_ready{false};
std::atomic<bool> g_survey_attempted{false};
std::atomic<bool> g_custom_hud_hidden{false};
std::atomic<bool> g_hud_missing_logged{false};
std::atomic<ULONGLONG> g_last_hud_hide_attempt_ms{0};
IUIElement* g_hidden_hud_screen = nullptr;
std::vector<IUIElement*> g_hidden_hud_elements;
std::atomic<uint32_t> g_frame{0};
std::atomic<bool> g_render_redirect{false};
std::atomic<int> g_render_slot{-1};

// Calibration state. Everything here is touched from the render thread, except
// the two atomics that the panel and OnRender hooks read every frame.
std::atomic<bool> g_calibrating{false};
int g_probe_start_frame = 0;
int g_calibration_slot = -1;
int g_calibration_frames_left = 0;
std::vector<int> g_calibration_candidates;
size_t g_calibration_index = 0;
OnRenderFn g_trial_original = nullptr;
std::vector<void*> g_probe_elements;
size_t g_probe_element_index = 0;
bool g_probe_elements_exhausted = false;
std::unordered_set<void*> g_drawable_vtables;

std::unordered_set<void**> g_patched_vtables;
std::unordered_map<void**, void*> g_onrender_originals;

// Lifecycle hooks can be called by an engine worker. They only publish pointer
// values atomically; all state transitions happen from Present on one thread.
std::atomic<void*> g_pending_main_level{nullptr};
std::atomic<void*> g_retired_level{nullptr};
std::atomic<uint32_t> g_retired_level_frame{0};
std::atomic<void*> g_selected_main_level{nullptr};

// ILevel::Deactivate can arrive from an engine worker. Serialising it with the
// bounded reads performed from Present prevents the level/player/UI objects
// from being destroyed while one of those calls is in flight. A recursive
// mutex also remains safe if the engine re-enters a hooked path on the same
// thread during deactivation.
std::recursive_mutex& g_level_access_lock = *new std::recursive_mutex();

// The currently selected main gameplay level. Render-thread-owned.
void* g_level = nullptr;

// Staging for the interface upload. Kept for the life of the process rather
// than allocated per frame: at 1240x1080 this is 5.4MB, and churning that
// every frame would be the most expensive thing the mod does.
std::vector<uint8_t> g_panel_pixels;
Page g_last_native_page = Page::kCount;
uint32_t g_bound_since_frame = UINT32_MAX;
ULONGLONG g_bound_since_ms = 0;
ULONGLONG g_last_map_sample_ms = 0;
ULONGLONG g_last_hud_sample_ms = 0;
ULONGLONG g_last_native_poll_ms = 0;

void** VTableOf(void* object) {
  void* vt = nullptr;
  return Read(object, &vt) ? static_cast<void**>(vt) : nullptr;
}

// The D3D11 state an OnRender hook temporarily replaces while it routes a UI
// element into the panel's render target.
struct SavedTargetState {
  ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* dsv = nullptr;
  D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
  UINT num_viewports = 0;
  ID3D11RenderTargetView* panel_rtv = nullptr;
};

bool BeginPanelDraw(ID3D11DeviceContext* context, Panel& panel, SavedTargetState& saved) {
  if (!context || !panel.ready()) return false;
  saved.panel_rtv = panel.AcquireRtv();
  if (!saved.panel_rtv) return false;

  context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved.rtvs, &saved.dsv);
  saved.num_viewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  context->RSGetViewports(&saved.num_viewports, saved.viewports);

  ID3D11RenderTargetView* target = saved.panel_rtv;
  context->OMSetRenderTargets(1, &target, nullptr);
  const D3D11_VIEWPORT viewport = {
      0.0f, 0.0f, static_cast<float>(panel.width()), static_cast<float>(panel.height()), 0.0f, 1.0f};
  context->RSSetViewports(1, &viewport);
  return true;
}

void EndPanelDraw(ID3D11DeviceContext* context, SavedTargetState& saved) {
  if (!context) return;
  context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved.rtvs, saved.dsv);
  context->RSSetViewports(saved.num_viewports, saved.viewports);
  for (auto* view : saved.rtvs) {
    if (view) view->Release();
  }
  if (saved.dsv) saved.dsv->Release();
  if (saved.panel_rtv) saved.panel_rtv->Release();
}

// ---------------------------------------------------------------- IAT hook
//
// Replaces one entry in a module's import address table.
//
// Chosen over patching the target function because the engine's exports are
// shared by three modules and a code patch would affect all of them, including
// calls this mod has no business observing. An IAT entry is one aligned
// pointer, owned by the loader, and swapping it changes exactly one caller.
bool HookImport(const wchar_t* module, const char* from_dll, const char* symbol,
                void* replacement, void** original) {
  HMODULE base = GetModuleHandleW(module);
  if (!base) return false;
  auto* bytes = reinterpret_cast<uint8_t*>(base);
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

  const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return false;
  auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(bytes + dir.VirtualAddress);

  for (; desc->Name; ++desc) {
    const char* name = reinterpret_cast<const char*>(bytes + desc->Name);
    if (_stricmp(name, from_dll) != 0) continue;

    auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(bytes + desc->FirstThunk);
    auto* orig = reinterpret_cast<IMAGE_THUNK_DATA64*>(
        bytes + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));

    for (; orig->u1.AddressOfData; ++thunk, ++orig) {
      if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG64) continue;
      auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(bytes + orig->u1.AddressOfData);
      if (strcmp(reinterpret_cast<const char*>(import->Name), symbol) != 0) continue;

      Unprotect open(&thunk->u1.Function, sizeof(void*));
      if (!open.ok()) return false;
      if (original) *original = reinterpret_cast<void*>(thunk->u1.Function);
      thunk->u1.Function = reinterpret_cast<ULONGLONG>(replacement);
      return true;
    }
  }
  return false;
}

// Reset every pointer owned by a level. Called only from Present; engine
// lifecycle callbacks never mutate these render-thread data structures.
void ClearLevel(const char* reason) {
  if (!g_level) return;
  DIS_INFO("[eng] releasing main ILevel %p (%s)", g_level,
           reason ? reason : "unspecified");
  g_selected_main_level.store(nullptr, std::memory_order_release);
  g_level = nullptr;
  g_survey_attempted.store(false, std::memory_order_release);
  g_calibrating.store(false, std::memory_order_release);
  g_calibration_slot = -1;
  g_probe_elements.clear();
  g_probe_element_index = 0;
  g_probe_elements_exhausted = false;
  g_custom_hud_hidden.store(false, std::memory_order_release);
  g_hud_missing_logged.store(false, std::memory_order_release);
  g_last_hud_hide_attempt_ms.store(0, std::memory_order_release);
  g_hidden_hud_screen = nullptr;
  g_hidden_hud_elements.clear();
  g_probe_hooks.RemoveAll();
  g_trial_hooks.RemoveAll();
  TheGameState().Unbind(reason);
  TheNativeMap().Reset();
  TheNativeUi().Reset();
  TheInterface().CancelPendingActions();
  g_last_native_page = Page::kCount;
  g_bound_since_frame = UINT32_MAX;
  g_bound_since_ms = 0;
  g_last_map_sample_ms = 0;
  g_last_hud_sample_ms = 0;
  g_last_native_poll_ms = 0;
  TheGame().Reset(reason);
}

void ApplyLevelLifecycle() {
  // A retirement wins over a pending publication of the same pointer. This
  // prevents a late IsMainLevel call from resurrecting a level being torn down.
  void* retired = g_retired_level.load(std::memory_order_acquire);
  if (retired) {
    void* expected = retired;
    g_pending_main_level.compare_exchange_strong(
        expected, nullptr, std::memory_order_acq_rel);
    if (g_level == retired) ClearLevel("level deactivated");
  }

  void* pending = g_pending_main_level.exchange(nullptr, std::memory_order_acq_rel);
  if (!pending || pending == g_level || pending == retired) return;
  // A different main level proves the retired pointer can no longer be
  // republished by a late callback from the old level.
  g_retired_level.store(nullptr, std::memory_order_release);
  if (g_level) ClearLevel("main level changed");
  g_level = pending;
  g_selected_main_level.store(pending, std::memory_order_release);
  TheGame().SetLevel(static_cast<ILevel*>(pending));
  DIS_INFO("[eng] selected main ILevel = %p", pending);
}

void PublishMainLevel(void* level, bool is_main) {
  if (is_main && level) {
    void* retired = g_retired_level.load(std::memory_order_acquire);
    if (level != retired) {
      g_pending_main_level.store(level, std::memory_order_release);
    } else {
      // Allow a legitimately reused allocation only after a two-second-ish
      // grace period; callbacks during Deactivate cannot resurrect it.
      const uint32_t age = g_frame.load(std::memory_order_relaxed) -
                           g_retired_level_frame.load(std::memory_order_relaxed);
      if (age >= 120) {
        void* expected = retired;
        if (g_retired_level.compare_exchange_strong(
                expected, nullptr, std::memory_order_acq_rel)) {
          g_pending_main_level.store(level, std::memory_order_release);
        }
      }
    }
  }
}

// This hook never guesses which of the several concurrent levels owns the
// playable world. It observes the return value produced by the game itself and
// publishes only the level that the engine identifies as main.
bool IsMainLevelHook(void* level) {
  const bool is_main = o_IsMainLevel ? o_IsMainLevel(level) : false;
  PublishMainLevel(level, is_main);
  return is_main;
}

// IsMainLevel is not necessarily called again when this proxy attaches after
// a level has already begun construction. GetIUIManager is queried throughout
// the UI lifetime, so use it as a second observation point while retaining the
// exact same discriminator. The original IsMainLevel implementation is the
// address gamedll itself imported; calling that saved pointer cannot recurse
// through our IAT hook.
void* GetUIManagerHook(void* level) {
  void* manager = o_GetUIManager ? o_GetUIManager(level) : nullptr;
  if (level && manager && o_IsMainLevel) {
    PublishMainLevel(level, o_IsMainLevel(level));
  }
  return manager;
}

struct TtlStringView {
  const char* data = nullptr;
  uint32_t size = 0;
  uint32_t capacity = 0;
};
static_assert(sizeof(TtlStringView) == 16,
              "ttl::string_base view ABI changed");

std::string BoundedTtlString(const void* value) {
  TtlStringView view = {};
  if (!value || !Read(value, &view) || !view.data || view.size == 0 ||
      view.size > 512 || view.capacity < view.size ||
      !Readable(view.data, view.size)) {
    return {};
  }
  return std::string(view.data, view.data + view.size);
}

int RegisterUIScreenHook(void* host, const void* script, int layer,
                         int group, bool transient,
                         const void* texture_pack, int selection) {
  const int id = o_RegisterUIScreen
      ? o_RegisterUIScreen(host, script, layer, group, transient,
                           texture_pack, selection)
      : -1;
  if (!host || id < 0) return id;

  std::string path = BoundedTtlString(script);
  std::transform(path.begin(), path.end(), path.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  const char* authored = nullptr;
  if (path.find("menuinventory.xui") != std::string::npos) {
    authored = "MenuInventory";
  } else if (path.find("menuupgradecharacter.xui") != std::string::npos) {
    authored = "MenuUpgradeCharacter";
  } else if (path.find("menuquestlog.xui") != std::string::npos) {
    authored = "MenuQuestLog";
  }
  if (authored) {
    ObserveNativeMenuRegistration(static_cast<ILevel*>(host), id, authored);
  }
  return id;
}

void DeactivateLevelHook(void* level) {
  {
    // Wait for any in-flight snapshot to finish, then publish retirement while
    // the same lock still excludes Present. Once published, every later
    // Present clears its level-owned caches before making another engine call.
    std::lock_guard<std::recursive_mutex> access(g_level_access_lock);
    if (level) {
      RetireNativeMenuHost(static_cast<ILevel*>(level));
      void* expected = level;
      const bool was_pending = g_pending_main_level.compare_exchange_strong(
          expected, nullptr, std::memory_order_acq_rel);
      if (was_pending ||
          g_selected_main_level.load(std::memory_order_acquire) == level) {
        g_retired_level_frame.store(g_frame.load(std::memory_order_relaxed),
                                    std::memory_order_relaxed);
        g_retired_level.store(level, std::memory_order_release);
      }
    }
  }
  // Do not hold the render/lifecycle lock through the engine's teardown. Its
  // implementation may wait for the render thread; keeping the lock here can
  // deadlock that wait. Retirement above already prevents any new reads.
  if (o_DeactivateLevel) o_DeactivateLevel(level);
}

void InstallLevelHooks() {
  if (!o_IsMainLevel &&
      HookImport(L"gamedll_x64_rwdi.dll", "engine_x64_rwdi.dll",
                 "?IsMainLevel@ILevel@@UEBA_NXZ",
                 reinterpret_cast<void*>(&IsMainLevelHook),
                 reinterpret_cast<void**>(&o_IsMainLevel))) {
    DIS_INFO("[eng] watching gamedll's import of ILevel::IsMainLevel");
  }
  if (!o_DeactivateLevel &&
      HookImport(L"gamedll_x64_rwdi.dll", "engine_x64_rwdi.dll",
                 "?Deactivate@ILevel@@QEAAXXZ",
                 reinterpret_cast<void*>(&DeactivateLevelHook),
                 reinterpret_cast<void**>(&o_DeactivateLevel))) {
    DIS_INFO("[eng] watching gamedll's import of ILevel::Deactivate");
  }
  if (!o_GetUIManager && o_IsMainLevel &&
      HookImport(L"gamedll_x64_rwdi.dll", "engine_x64_rwdi.dll",
                 "?GetIUIManager@ILevel@@QEAAPEAVIUIManager@@XZ",
                 reinterpret_cast<void*>(&GetUIManagerHook),
                 reinterpret_cast<void**>(&o_GetUIManager))) {
    DIS_INFO("[eng] watching GetIUIManager as a late main-level observation point");
  }
  if (!o_RegisterUIScreen &&
      HookImport(L"gamedll_x64_rwdi.dll", "engine_x64_rwdi.dll",
                 "?RegisterUIScreen@ILevel@@QEAAHAEBV?$string_base@D@ttl@@HH_N0H@Z",
                 reinterpret_cast<void*>(&RegisterUIScreenHook),
                 reinterpret_cast<void**>(&o_RegisterUIScreen))) {
    DIS_INFO("[eng] watching authored menu registrations");
  }
}

// ---------------------------------------------------------------- UI redirection
//
// The engine's UI tree is a hierarchy of IUIElement objects whose OnRender
// method draws exactly that element into the current render target. Dead
// Island's UI is not a separate movie/renderer like Human Revolution's
// Scaleform, so the clean seam is the virtual OnRender call itself: bind the
// panel view for the duration of the call, call the original, and put the
// game's previous target back. The game therefore draws the world on the top
// display and every native HUD/menu element on the bottom display.

void __stdcall OnRenderHook(void* self);

void __stdcall TrialOnRenderHook(void* self) {
  OnRenderFn original = g_trial_original;
  if (!original) return;
  if (!g_context || !ThePanel().ready()) {
    original(self);
    return;
  }

  SavedTargetState saved;
  if (BeginPanelDraw(g_context, ThePanel(), saved)) {
    original(self);
    EndPanelDraw(g_context, saved);
  } else {
    original(self);
  }
}

void __stdcall OnRenderHook(void* self) {
  void** vtable = VTableOf(self);
  if (!vtable) return;
  const auto found = g_onrender_originals.find(vtable);
  if (found == g_onrender_originals.end()) return;

  const OnRenderFn original = reinterpret_cast<OnRenderFn>(found->second);
  if (!original) return;
  if (!g_render_redirect.load(std::memory_order_relaxed) ||
      g_render_slot.load(std::memory_order_relaxed) < 0 || !g_context ||
      !ThePanel().ready()) {
    original(self);
    return;
  }

  SavedTargetState saved;
  if (BeginPanelDraw(g_context, ThePanel(), saved)) {
    original(self);
    EndPanelDraw(g_context, saved);
  } else {
    original(self);
  }
}

bool PatchVtable(void** vtable) {
  if (!vtable) return false;
  const int slot = g_render_slot.load(std::memory_order_relaxed);
  if (slot < 0) return false;

  if (g_patched_vtables.find(vtable) != g_patched_vtables.end()) return true;

  void* original = nullptr;
  if (!Read(vtable + slot, &original)) return false;
  if (original == reinterpret_cast<void*>(&OnRenderHook)) {
    g_patched_vtables.insert(vtable);
    g_onrender_originals[vtable] = nullptr;
    return true;
  }
  if (!Executable(original, 1)) return false;

  if (!g_hooks.Add(vtable, static_cast<uint32_t>(slot),
                   reinterpret_cast<void*>(&OnRenderHook))) {
    return false;
  }
  g_patched_vtables.insert(vtable);
  g_onrender_originals[vtable] = original;
  return true;
}

void PatchElementTree(void* element, int depth, int& budget) {
  if (!element || depth > 12 || budget <= 0) return;
  void* vt = nullptr;
  if (Read(element, &vt)) PatchVtable(static_cast<void**>(vt));
  --budget;

  const Engine& e = TheEngine();
  const int count = e.ChildCount(element);
  if (count < 0 || count > 4096) return;
  for (int i = 0; i < count && budget > 0; ++i) {
    void* child = e.Child(element, i);
    if (!child || !Readable(child, sizeof(void*))) continue;
    PatchElementTree(child, depth + 1, budget);
  }
}

void ScanAndPatchUi() {
  if (!g_render_redirect.load(std::memory_order_relaxed) ||
      g_render_slot.load(std::memory_order_relaxed) < 0 ||
      !g_level) {
    return;
  }
  const Engine& e = TheEngine();
  if (!e.IsUIManagerVisible(g_level)) return;

  std::vector<void*> screens;
  if (!e.GetLoadedScreens(g_level, &screens, 4096)) return;

  int budget = 4096;
  for (void* screen : screens) PatchElementTree(screen, 0, budget);
}

void HideNativeHudOnce() {
  if (!g_level || !TheEngine().IsUIManagerVisible(g_level)) return;

  const int id = Cfg().GetInt("ui.hide_hud_screen", 1024);
  IUIElement* screen = TheEngine().FindScreen(g_level, id);
  if (!screen) {
    g_hidden_hud_screen = nullptr;
    g_hidden_hud_elements.clear();
    if (!g_hud_missing_logged.exchange(true, std::memory_order_acq_rel)) {
      DIS_WARN("[custom] HUD screen %d was not found yet", id);
    }
    return;
  }
  g_hud_missing_logged.store(false, std::memory_order_release);

  const ULONGLONG now = GetTickCount64();
  const ULONGLONG previous =
      g_last_hud_hide_attempt_ms.load(std::memory_order_relaxed);
  const bool refresh = screen != g_hidden_hud_screen || previous == 0 ||
                       now - previous >= 1000;
  if (refresh) {
    g_last_hud_hide_attempt_ms.store(now, std::memory_order_relaxed);
    g_hidden_hud_screen = screen;
    g_hidden_hud_elements.clear();

    // Older builds suppressed screen 1024 itself. Restore its render opacity,
    // then hide only the five authored clusters replaced by the companion HUD.
    // Crosshair, XP/fury, prompts, objectives, notifications, radial selector,
    // co-op status and every other vanilla HUD element remain untouched.
    TheEngine().SetOpacity(screen, 1.0f);
    static constexpr const char* kHiddenPaths[] = {
        "HudPlayer/boold_drops",
        "HudPlayer/G_Group/I_Blood",
        "HudPlayer/G_Group/G_BloodMask",
        "HudPlayer/G_Group/I_BackMask",
        "HudPlayer/G_Group/I_Back",
        "HudPlayer/G_Group/I_Bar",
        "HudPlayer/G_Group/I_BarMask",
        // Alternate layout used by older/SD HUD variants.
        "HudPlayer/I_Blood",
        "HudPlayer/G_BloodMask",
        "HudPlayer/I_BackMask",
        "HudPlayer/I_Back",
        "HudPlayer/I_Bar",
        "HudPlayer/I_BarMask",
        "HudStamina",
        "HudRadar",
        "HudInventory",
        "HudHeldItem",
    };
    for (const char* path : kHiddenPaths) {
      if (IUIElement* element = TheEngine().FindElementByPath(screen, path)) {
        g_hidden_hud_elements.push_back(element);
      }
    }
    static bool logged = false;
    if (!logged) {
      logged = true;
      DIS_INFO("[custom] selectively suppressing %d native HUD elements; "
               "crosshair and gameplay messaging remain visible",
               static_cast<int>(g_hidden_hud_elements.size()));
    }
  }

  // Authored HUD timelines can restore child opacity each update. Reapply on
  // every Present using the cached, level-owned pointers; paths are refreshed
  // once a second and whenever the HUD screen object is replaced.
  for (IUIElement* element : g_hidden_hud_elements) {
    TheEngine().SetOpacity(element, 0.0f);
  }
  g_custom_hud_hidden.store(!g_hidden_hud_elements.empty(),
                            std::memory_order_release);
}

// Advances the player search and re-reads the bound values. One call per
// panel frame.
//
// This replaces reading `ILevel::GetVarSet`, which is what the first version
// did and why every vital it printed was 0.00. That var set holds the *level's*
// script variables -- the things a mission script sets -- and the player's
// health was never among them. Worse, the read went through
// `CDynamicRTTIObject::GetFloat`, a virtual called at its exported address, so
// it ran the base implementation and would have returned zero even pointed at
// the right object.
//
// The player's health lives on the player, and it is reached by name through
// the non-virtual CRTTIObject reflection API. See Rtti.h.
void UpdateGameState() {
  Game& game = TheGame();
  if (!game.Tick()) {
    TheGameState().Unbind("no player");
    return;
  }
  CRTTIObject* player = game.player();
  if (!player) {
    TheGameState().Unbind("player went away");
    return;
  }
  // Bind is a no-op once bound to this pointer; Sample is the per-frame read.
  TheGameState().Bind(player);
  TheGameState().Sample(player);
}

bool PanelContainsInk() {
  Panel& panel = ThePanel();
  IDXGISwapChain* swapchain = panel.swapchain();
  if (!swapchain || !g_device || !g_context) return false;

  ID3D11Texture2D* back = nullptr;
  if (FAILED(swapchain->GetBuffer(0, IID_ID3D11Texture2D,
                                  reinterpret_cast<void**>(&back))) || !back) {
    return false;
  }

  D3D11_TEXTURE2D_DESC source_desc;
  back->GetDesc(&source_desc);

  D3D11_TEXTURE2D_DESC staging_desc = {};
  staging_desc.Width = source_desc.Width;
  staging_desc.Height = source_desc.Height;
  staging_desc.MipLevels = 1;
  staging_desc.ArraySize = 1;
  staging_desc.Format = source_desc.Format;
  staging_desc.SampleDesc.Count = 1;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

  ID3D11Texture2D* staging = nullptr;
  if (FAILED(g_device->CreateTexture2D(&staging_desc, nullptr, &staging)) || !staging) {
    back->Release();
    return false;
  }

  g_context->CopyResource(staging, back);
  back->Release();

  D3D11_MAPPED_SUBRESOURCE mapped;
  bool found = false;
  if (SUCCEEDED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
    const auto* pixels = static_cast<const uint8_t*>(mapped.pData);
    const int step = 16;
    for (UINT y = 0; y < staging_desc.Height && !found; y += step) {
      for (UINT x = 0; x < staging_desc.Width && !found; x += step) {
        const size_t at = static_cast<size_t>(y) * mapped.RowPitch +
                          static_cast<size_t>(x) * 4;
        const uint8_t a = pixels[at];
        const uint8_t b = pixels[at + 1];
        const uint8_t c = pixels[at + 2];
        // The panel was cleared to black immediately before the candidate frame.
        // Alpha is always 255 after the clear, so only colour channels are
        // useful evidence that a draw actually happened.
        if (a > 24 || b > 24 || c > 24) {
          found = true;
        }
      }
    }
    g_context->Unmap(staging, 0);
  }
  staging->Release();
  return found;
}

void CommitRenderSlot(int slot) {
  g_trial_hooks.RemoveAll();
  g_calibrating.store(false, std::memory_order_release);
  g_calibration_slot = -1;
  g_render_slot.store(slot, std::memory_order_release);
  g_render_redirect.store(true, std::memory_order_release);
  DIS_INFO("[ui] OnRender identified as vtable slot %d; native UI now targets the panel", slot);
  ScanAndPatchUi();
}

void AdvanceCalibrationCandidate() {
  ++g_calibration_index;
  if (g_calibration_index >= g_calibration_candidates.size()) {
    g_trial_hooks.RemoveAll();
    g_calibrating.store(false, std::memory_order_release);
    g_calibration_slot = -1;
    g_render_redirect.store(false, std::memory_order_release);
    g_render_slot.store(-1, std::memory_order_release);
    DIS_WARN("[ui] no vtable slot drew to the panel during calibration; keeping mirror fallback");
    return;
  }

  const int slot = g_calibration_candidates[g_calibration_index];
  void** vtable = UiProbeVtable();
  void* original = UiProbeOriginal(slot);
  if (!vtable || !original) {
    AdvanceCalibrationCandidate();
    return;
  }

  g_trial_hooks.RemoveAll();
  g_trial_original = reinterpret_cast<OnRenderFn>(original);
  g_calibration_slot = slot;
  g_calibration_frames_left = Cfg().GetInt("ui.trial_frames", 2);
  if (g_trial_hooks.Add(vtable, static_cast<uint32_t>(slot),
                        reinterpret_cast<void*>(&TrialOnRenderHook))) {
    DIS_INFO("[ui] trying candidate vtable slot %d", slot);
  } else {
    AdvanceCalibrationCandidate();
  }
}

void CalibrationFrame() {
  if (!g_calibrating.load(std::memory_order_relaxed) || g_calibration_slot < 0) return;
  if (PanelContainsInk()) {
    CommitRenderSlot(g_calibration_slot);
    return;
  }

  if (--g_calibration_frames_left <= 0) {
    g_trial_hooks.RemoveAll();
    AdvanceCalibrationCandidate();
  }
}

void CollectProbeElement(void* element, int depth, int& budget) {
  if (!element || depth > 12 || budget <= 0 || g_probe_elements.size() > 2048) return;

  const Engine& e = TheEngine();
  const int count = e.ChildCount(element);
  // Containers are rarely the objects that issue OnRender. The draw calls come
  // from visible leaves -- text, images, shapes, and canvas elements -- so those
  // are the objects worth probing.
  if (count == 0 && e.IsVisible(element)) {
    g_probe_elements.push_back(element);
    --budget;
  }
  if (count < 0 || count > 4096) return;
  for (int i = 0; i < count && budget > 0; ++i) {
    void* child = e.Child(element, i);
    if (!child || !Readable(child, sizeof(void*))) continue;
    CollectProbeElement(child, depth + 1, budget);
  }
}

bool IsDrawableVtable(void** vtable) {
  return vtable && g_drawable_vtables.find(static_cast<void*>(vtable)) !=
                        g_drawable_vtables.end();
}

void BuildDrawableVtableSet() {
  if (!g_drawable_vtables.empty()) return;
  HMODULE engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
  if (!engine) return;

  const char* names[] = {
      "??_7IUIImage@@6B@",
      "??_7IUIText@@6B@",
      "??_7IUICanvas@@6B@",
      "??_7IUIAARectangle@@6B@",
      "??_7IUIShape@@6B@",
      "??_7IUIElementView@@6B@",
      "??_7IUIScene@@6B@",
  };
  for (const char* name : names) {
    FARPROC address = GetProcAddress(engine, name);
    if (address) g_drawable_vtables.insert(reinterpret_cast<void*>(address));
  }
}

void CollectProbeElements() {
  g_probe_elements.clear();
  g_probe_element_index = 0;
  g_probe_elements_exhausted = false;
  if (!g_level) return;

  const Engine& e = TheEngine();
  std::vector<void*> screens;
  if (!e.GetLoadedScreens(g_level, &screens, 4096)) return;

  int budget = 2048;
  for (void* screen : screens) {
    const int id = e.ScreenId(screen);
    if (id < 0 || !e.IsScreenVisible(g_level, id)) continue;
    CollectProbeElement(screen, 0, budget);
  }

  BuildDrawableVtableSet();
  std::stable_sort(g_probe_elements.begin(), g_probe_elements.end(),
                   [](void* left, void* right) {
                     const bool dl = IsDrawableVtable(VTableOf(left));
                     const bool dr = IsDrawableVtable(VTableOf(right));
                     // Concrete UI object vtables are not the exported interface
                     // vtables. Probe those first, and leave base-interface
                     // leaves -- which are rarely the objects issuing OnRender --
                     // until the end.
                     if (dl != dr) return !dl;
                     return left < right;
                   });
}

void TryBeginCalibration() {
  if (g_calibrating.load(std::memory_order_relaxed) ||
      g_render_slot.load(std::memory_order_relaxed) >= 0 ||
      !Cfg().GetBool("ui.calibrate", true) ||
      !g_level || g_probe_elements_exhausted) {
    return;
  }

  const Engine& e = TheEngine();
  if (!e.IsUIManagerVisible(g_level)) return;

  if (g_probe_elements.empty()) CollectProbeElements();
  if (g_probe_element_index >= g_probe_elements.size()) {
    g_probe_elements_exhausted = true;
    DIS_WARN("[ui] no UI element vtable produced per-frame calls; keeping mirror fallback");
    return;
  }

  void* element = g_probe_elements[g_probe_element_index];
  if (!element) {
    ++g_probe_element_index;
    return;
  }

  g_probe_hooks.RemoveAll();
  UiProbeReset();
  if (UiProbeInstall(element, &g_probe_hooks)) {
    g_probe_start_frame = static_cast<int>(g_frame.load(std::memory_order_relaxed));
    g_calibrating.store(true, std::memory_order_release);
    DIS_INFO("[ui] calibration probe installed on element %zu of %zu",
             g_probe_element_index, g_probe_elements.size());
    return;
  }
  ++g_probe_element_index;
}

void FinishCalibrationIfReady() {
  if (!g_calibrating.load(std::memory_order_relaxed) ||
      g_calibration_slot >= 0 ||
      g_render_slot.load(std::memory_order_relaxed) >= 0) {
    return;
  }

  const int elapsed = static_cast<int>(g_frame.load(std::memory_order_relaxed)) -
                      g_probe_start_frame;
  if (elapsed < Cfg().GetInt("ui.probe_frames", 120)) return;

  UiProbeReport(static_cast<uint32_t>(elapsed));
  g_calibration_candidates.clear();
  const int slot_count = UiProbeSlotCount();
  for (int slot = 0; slot < slot_count; ++slot) {
    const uint64_t calls = dis_ui_calls[slot];
    if (calls == 0) continue;
    const double per_frame = static_cast<double>(calls) / elapsed;
    if (per_frame >= 0.5) g_calibration_candidates.push_back(slot);
  }

  // Prefer slots whose call rate is close to once per frame; those are the
  // most likely to be per-element render/update calls rather than occasional
  // lifecycle methods.
  std::sort(g_calibration_candidates.begin(), g_calibration_candidates.end(),
            [&](int left, int right) {
              const double lf = static_cast<double>(dis_ui_calls[left]) / elapsed;
              const double rf = static_cast<double>(dis_ui_calls[right]) / elapsed;
              const double ld = std::abs(lf - std::round(lf));
              const double rd = std::abs(rf - std::round(rf));
              if (ld != rd) return ld < rd;
              return lf > rf;
            });

  g_probe_hooks.RemoveAll();
  if (g_calibration_candidates.empty()) {
    g_calibrating.store(false, std::memory_order_release);
    ++g_probe_element_index;
    DIS_WARN("[ui] probe element %zu had no per-frame vtable slots; trying the next element",
             g_probe_element_index);
    return;
  }

  g_calibration_index = 0;
  g_calibration_slot = -1;
  g_calibration_frames_left = 0;
  DIS_INFO("[ui] %u candidate slot(s): %s",
           static_cast<unsigned>(g_calibration_candidates.size()),
           [&] {
             std::string out;
             for (size_t i = 0; i < g_calibration_candidates.size(); ++i) {
               if (i) out += ", ";
               out += std::to_string(g_calibration_candidates[i]);
             }
             return out;
           }().c_str());
  g_calibration_slot = g_calibration_candidates[0];
  g_trial_hooks.RemoveAll();
  void** vtable = UiProbeVtable();
  void* original = UiProbeOriginal(g_calibration_slot);
  if (!vtable || !original ||
      !g_trial_hooks.Add(vtable, static_cast<uint32_t>(g_calibration_slot),
                         reinterpret_cast<void*>(&TrialOnRenderHook))) {
    AdvanceCalibrationCandidate();
    return;
  }
  g_trial_original = reinterpret_cast<OnRenderFn>(original);
  g_calibration_frames_left = Cfg().GetInt("ui.trial_frames", 2);
  DIS_INFO("[ui] trying candidate vtable slot %d", g_calibration_slot);
}

// ---------------------------------------------------------------- UI survey
//
// Walks a UI element tree and writes it down: name, position, size, scale and
// visibility, indented by depth. This is the map the layout will be designed
// against -- and, unlike the previous mod, it is a map read out of the running
// game rather than inferred from a file.
// Element names are OFF by default, and that is not caution for its own sake.
//
// IUIElement::GetDebugId returns `const ttl::string_base<char>` *by value*: a
// non-trivial class constructed into a hidden buffer the caller supplies, plus
// an int argument whose meaning is not established. Reading it means handing
// the engine a stack buffer of a size guessed from nothing and hoping the type
// fits. It does not crash on a wrong guess -- it smashes the stack, on the
// render thread, during a level load.
//
// That is exactly what happened: the survey reached "screen HUD id=1024" and
// the process died on the first element. Position, size, scale and visibility
// are all plain scalar returns and carry no such risk, and they are enough to
// map the tree. The name is a convenience, and a convenience is not worth a
// crash -- so it is behind advanced.elementnames, off unless asked for.
bool g_read_element_names = false;

void DumpElement(void* element, int depth, int& budget) {
  if (!element || budget <= 0) return;
  const Engine& e = TheEngine();

  std::string id = g_read_element_names ? e.ElementId(element) : std::string();
  const Vec3 pos = e.GetPos(element);
  const Vec3 scale = e.GetScale(element);
  const float w = e.GetSizeX(element);
  const float h = e.GetSizeY(element);
  const bool visible = e.IsVisible(element);

  char indent[33];
  const int pad = depth < 16 ? depth * 2 : 32;
  memset(indent, ' ', pad);
  indent[pad] = '\0';

  --budget;
  DIS_INFO("[ui] %s%-28s pos=%.0f,%.0f size=%.0fx%.0f scale=%.2f %s", indent,
           id.empty() ? "<element>" : id.c_str(), pos.x, pos.y, w, h, scale.x,
           visible ? "" : "(hidden)");

  // Bounded depth as well as budget. hud.xui nests deeply, and an unbounded
  // recursion through a tree read out of another process is a stack overflow
  // waiting for a malformed child pointer.
  if (depth >= 12) return;
  const int n = e.ChildCount(element);
  if (n < 0 || n > 4096) return;  // implausible count: do not trust it
  for (int i = 0; i < n && budget > 0; ++i) {
    DumpElement(e.Child(element, i), depth + 1, budget);
  }
}

void SurveyUI() {
  if (!Cfg().GetBool("advanced.survey", false)) return;
  if (!g_level) {
    DIS_WARN("[ui] no level captured yet; the game has not asked for its UI manager");
    return;
  }
  const Engine& e = TheEngine();
  std::vector<void*> screens;
  const int max_id = Cfg().GetInt("advanced.maxscreenid", 4096);
  if (!e.GetLoadedScreens(g_level, &screens, max_id)) {
    DIS_WARN("[ui] no screens registered over ids 0..%d", max_id - 1);
    return;
  }
  DIS_INFO("[ui] %u screen(s) reachable by id", static_cast<unsigned>(screens.size()));

  g_read_element_names = Cfg().GetBool("advanced.elementnames", false);
  int budget = Cfg().GetInt("advanced.surveybudget", 300);
  for (void* screen : screens) {
    void* owner = screen;
    const std::string name = e.ScreenName(owner);
    DIS_INFO("[ui] ---- screen %-30s id=%d ----",
             name.empty() ? "<unnamed>" : name.c_str(), e.ScreenId(owner));
    DumpElement(screen, 1, budget);

    // Install the slot probe on the first element that has children -- a
    // container that is actually part of the live HUD, rather than the screen
    // root, whose class may not be one that draws.
    if (Cfg().GetBool("advanced.slotprobe", false)) {
      const int kids = e.ChildCount(screen);
      for (int i = 0; i < kids; ++i) {
        void* child = e.Child(screen, i);
        if (!child) continue;
        if (UiProbeInstall(child, &g_probe_hooks)) {
          DIS_INFO("[probe] installed on child %d of screen id %d", i, e.ScreenId(screen));
          break;
        }
      }
    }
    if (budget <= 0) { DIS_WARN("[ui] element budget reached; tree truncated"); break; }
  }
}

// ---------------------------------------------------------------- startup
void AbandonStartup() {
  StopTouchInput();
  ThePanel().Destroy();
  if (g_context) { g_context->Release(); g_context = nullptr; }
  if (g_device) { g_device->Release(); g_device = nullptr; }
  if (g_factory) { g_factory->Release(); g_factory = nullptr; }
  g_ready.store(false, std::memory_order_release);
}

bool Start() {
  if (!g_swapchain) return false;
  // gamedll can be mapped after the first DXGI export is used, so retry the
  // narrow lifecycle hooks here. InstallLevelHooks is idempotent.
  InstallLevelHooks();
  const HRESULT device_hr = g_swapchain->GetDevice(
      __uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device));
  if (FAILED(device_hr) || !g_device) {
    DIS_ERR("[d3d] game swapchain has no D3D11 device (0x%08lX); standing down",
            static_cast<unsigned long>(device_hr));
    AbandonStartup();
    return false;
  }
  g_device->GetImmediateContext(&g_context);
  if (!g_context) {
    DIS_ERR("[d3d] game device has no immediate context; standing down");
    AbandonStartup();
    return false;
  }

  DXGI_SWAP_CHAIN_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  if (SUCCEEDED(g_swapchain->GetDesc(&desc))) {
    DIS_INFO("[d3d] game swapchain %ux%u windowed=%d hwnd=%p", desc.BufferDesc.Width,
             desc.BufferDesc.Height, desc.Windowed, static_cast<void*>(desc.OutputWindow));
  }

  // The device's own factory, walked the same way the game gets it. A swapchain
  // created from any other factory is unreliable.
  IDXGIDevice* dxgi_device = nullptr;
  if (SUCCEEDED(g_device->QueryInterface(__uuidof(IDXGIDevice),
                                         reinterpret_cast<void**>(&dxgi_device))) &&
      dxgi_device) {
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(dxgi_device->GetAdapter(&adapter)) && adapter) {
      adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&g_factory));
      adapter->Release();
    }
    dxgi_device->Release();
  }

  if (!TheEngine().Resolve()) {
    DIS_ERR("[mod] the engine's UI API did not resolve; standing down");
    AbandonStartup();
    return false;
  }

  // The reflection API is a separate resolve from the UI one, and a separate
  // failure: the panel is still worth putting up with a HUD that says it has no
  // data, so this does not stand the mod down.
  if (!TheRtti().Resolve(GetModuleHandleW(L"engine_x64_rwdi.dll"))) {
    DIS_ERR("[mod] CRTTIObject reflection did not resolve;"
            " the panel will draw but every value will read unavailable");
  }
  TheGame().set_scan_limit(Cfg().GetInt("game.scan_limit", 65536));
  TheGame().set_scan_budget(Cfg().GetInt("game.scan_budget", 512));
  TheGame().set_use_enumeration(Cfg().GetBool("game.enumerate_objects", false));
  TheGame().set_find_actors(Cfg().GetBool("game.find_actors", true));

  if (!o_IsMainLevel) {
    DIS_WARN("[eng] gamedll's IsMainLevel import is not available yet;"
             " no level will be touched until the narrow lifecycle hook installs");
  }

  // Match the game's back-buffer format, or CopySubresourceRegion refuses the
  // blit and the panel stays black with nothing but a debug-layer message.
  {
    DXGI_SWAP_CHAIN_DESC d;
    ZeroMemory(&d, sizeof(d));
    if (SUCCEEDED(g_swapchain->GetDesc(&d))) {
      ThePanel().SetFormat(static_cast<unsigned>(d.BufferDesc.Format));
      DIS_INFO("[panel] adopting the game's back-buffer format %u",
               static_cast<unsigned>(d.BufferDesc.Format));
    }
  }

  if (!ThePanel().Create(g_factory, g_device, g_context, nullptr)) {
    DIS_ERR("[mod] no panel; standing down, the game is unaffected");
    AbandonStartup();
    return false;
  }
  StartTouchInput();
  g_ready.store(true);
  DIS_INFO("[mod] up. Surveying the live UI tree once the level appears.");
  return true;
}

HRESULT __stdcall PresentHook(IDXGISwapChain* self, UINT interval, UINT flags) {
  if (self == ThePanel().swapchain()) {
    return o_Present ? o_Present(self, interval, flags) : S_OK;
  }
  // The vtable hook sees movie, overlay and helper swapchains too. Once the
  // first game chain is selected, they must never tick lifecycle/UI state.
  if (g_started.load(std::memory_order_acquire) && self != g_swapchain) {
    return o_Present ? o_Present(self, interval, flags) : S_OK;
  }
  if (!g_started.exchange(true)) {
    g_swapchain = self;
    if (!Start()) {
      // A helper/movie chain can present before the real game chain, and a
      // transient device/panel failure must not permanently latch onto it.
      g_swapchain = nullptr;
      g_started.store(false, std::memory_order_release);
      return o_Present ? o_Present(self, interval, flags) : S_OK;
    }
  }
  if (self != g_swapchain) {
    return o_Present ? o_Present(self, interval, flags) : S_OK;
  }
  const uint32_t frame = g_frame.fetch_add(1, std::memory_order_relaxed);
  if ((frame % 120) == 0 &&
      (!o_IsMainLevel || !o_DeactivateLevel || !o_GetUIManager ||
       !o_RegisterUIScreen)) {
    // gamedll may map after DXGI and even after the first renderer Present.
    // Retry the narrow IAT hooks at low frequency until both are installed.
    InstallLevelHooks();
  }
  if (g_ready.load(std::memory_order_relaxed)) {
    std::unique_lock<std::recursive_mutex> access(g_level_access_lock);
    const ULONGLONG now_ms = GetTickCount64();
    Panel& panel = ThePanel();
    ApplyLevelLifecycle();
    PumpTouchInput(panel);

    if (Cfg().GetBool("ui.custom_hud", true)) {
      UpdateGameState();
      // Screen 1024 also exists in a front-end level. Hiding it before a real
      // player is bound can blank the start screen, so suppress the native HUD
      // only after gameplay data has been proven live.
      const bool player_bound = TheGameState().bound();
      if (player_bound) {
        HideNativeHudOnce();
        if (g_bound_since_frame == UINT32_MAX) {
          g_bound_since_frame = frame;
          g_bound_since_ms = now_ms;
          g_last_map_sample_ms = 0;
          g_last_hud_sample_ms = 0;
        }
      } else {
        g_bound_since_frame = UINT32_MAX;
        g_bound_since_ms = 0;
      }
      // Game::Tick already requires a non-loading level and a stable live
      // player before binding. A short wall-clock settling window keeps native
      // menus out of save construction without turning 60 rendered frames
      // into a 10-30 second wait when the game is temporarily running slowly.
      const bool post_load_ready = player_bound &&
          g_bound_since_ms != 0 && now_ms - g_bound_since_ms >= 200;

      const Vitals& v = TheGameState().vitals();

      // Draw the interface. Render answers false when nothing a viewer could
      // see has changed, and then the upload is skipped entirely -- at
      // 1240x1080 a repaint is 5.4MB across the bus, and a HUD that has not
      // moved should not be paying that sixty times a second.
      Interface& ui = TheInterface();
      if (post_load_ready) {
        ui.ProcessPendingActions();
      } else {
        // A player object can bind while the save is still constructing its
        // native menu controllers. Never let an early quick-slot tap open one
        // during that window; stale queued work is less dangerous than replaying
        // a mutation against the next fully-loaded frame.
        ui.CancelPendingActions();
      }
      // Position/heading are sampled at 10 Hz only while visible. Wall-clock
      // cadence remains responsive even during a low-frame-rate transition.
      if ((ui.page() == Page::kMap || ui.page() == Page::kStatus) &&
          player_bound &&
          (g_last_map_sample_ms == 0 ||
           now_ms - g_last_map_sample_ms >= 100)) {
        g_last_map_sample_ms = now_ms;
        TheNativeMap().Update(static_cast<ILevel*>(g_level),
                              TheGame().player_control(), TheEngine());
      }
      // Screen 1024 remains logically alive at zero opacity. Sampling its
      // small text feed at 10 Hz provides equipped-item/ammo/XP details without
      // walking a hidden menu controller or adding per-frame UI work.
      if (Cfg().GetBool("ui.native_hud_snapshot", true) && player_bound &&
          (g_last_hud_sample_ms == 0 ||
           now_ms - g_last_hud_sample_ms >= 100)) {
        g_last_hud_sample_ms = now_ms;
        TheNativeUi().UpdateHud(static_cast<ILevel*>(g_level));
      }
      // Inventory and skills are sampled from the live PlayerDI/LogicalPlayer
      // models. Never open a transparent vanilla menu: those XUI controllers
      // contain no rows until the real menu is shown and can alter gameplay
      // mode during save construction. Page changes only reset the bounded
      // read cadence.
      if (ui.page() != g_last_native_page) {
        g_last_native_page = ui.page();
        g_last_native_poll_ms = 0;
      }
      NativeUiDomain page_domain = NativeUiDomain::kAll;
      if (ui.page() == Page::kInventory)
        page_domain = NativeUiDomain::kInventory;
      if (ui.page() == Page::kMissions)
        page_domain = NativeUiDomain::kMissions;
      if (ui.page() == Page::kSkills)
        page_domain = NativeUiDomain::kSkills;
      const bool bridge_enabled =
          Cfg().GetBool("ui.native_menu_bridge", true);
      if (bridge_enabled && post_load_ready &&
          page_domain != NativeUiDomain::kAll &&
          (g_last_native_poll_ms == 0 ||
           now_ms - g_last_native_poll_ms >= 100)) {
        g_last_native_poll_ms = now_ms;
        TheNativeUi().Update(static_cast<ILevel*>(g_level), page_domain);
      }
      if (ui.Create(panel.width(), panel.height())) {
        if (ui.Render(TheGame(), TheGameState())) {
          if (g_panel_pixels.size() !=
              static_cast<size_t>(panel.width()) * panel.height() * 4) {
            g_panel_pixels.assign(
                static_cast<size_t>(panel.width()) * panel.height() * 4, 0);
          }
          ui.canvas().CopyTo(g_panel_pixels.data(),
                             static_cast<size_t>(panel.width()) * 4);
          panel.UploadPixels(g_context, g_panel_pixels.data(), panel.width(),
                             panel.height());
        }
      }

      if (frame == 1 || (frame % 300) == 0) {
        if (v.usable()) {
          DIS_INFO("[custom] health %.1f/%.1f  stamina %.1f/%.1f  fury %.1f  lvl %d",
                   v.health.v, v.max_health.valid ? v.max_health.v : -1.0,
                   v.stamina.valid ? v.stamina.v : -1.0,
                   v.max_stamina.valid ? v.max_stamina.v : -1.0,
                   v.fury.valid ? v.fury.v : -1.0,
                   v.level.valid ? v.level.i() : -1);
        } else {
          Game& game = TheGame();
          DIS_INFO("[custom] no player yet (searching=%d scanned=%d class='%s')",
                   game.searching() ? 1 : 0, game.scanned(),
                   game.player_class().c_str());
        }
      }
      // Never hold the lifecycle lock across the driver's Present. A render
      // wait may depend on an engine worker that is concurrently retiring the
      // level; the data reads above are complete, so release before entering
      // the driver.
      access.unlock();
      panel.Present();
      return o_Present ? o_Present(self, interval, flags) : S_OK;
    }

    if (g_calibrating.load(std::memory_order_relaxed) && g_calibration_slot >= 0) {
      CalibrationFrame();
    }

    if (g_render_redirect.load(std::memory_order_relaxed) &&
        g_calibration_slot < 0 &&
        (frame % 120) == 0) {
      ScanAndPatchUi();
    }

    // Mirror mode is the fallback for the brief interval before the OnRender
    // slot is identified and for an unsupported UI build. Once redirection is
    // active the game draws its own UI into the panel, so copying the top
    // frame would overwrite it.
    if (!g_render_redirect.load(std::memory_order_relaxed)) {
      ID3D11Texture2D* back = nullptr;
      if (SUCCEEDED(self->GetBuffer(0, IID_ID3D11Texture2D,
                                    reinterpret_cast<void**>(&back))) && back) {
        const int sx = Cfg().GetInt("mirror.sourcex", 0);
        const int sy = Cfg().GetInt("mirror.sourcey", 0);
        const int sw = Cfg().GetInt("mirror.width", 1240);
        const int sh = Cfg().GetInt("mirror.height", 720);
        const int dx = Cfg().GetInt("mirror.destx", 0);
        const int dy = Cfg().GetInt("mirror.desty", 180);
        panel.CopyFrom(back, sx, sy, sw, sh, dx, dy);
        back->Release();
      }
    }

    panel.Present();

    if (g_calibrating.load(std::memory_order_relaxed) && g_calibration_slot < 0) {
      FinishCalibrationIfReady();
    }
    if (!g_calibrating.load(std::memory_order_relaxed) &&
        g_render_slot.load(std::memory_order_relaxed) < 0 &&
        g_level) {
      TryBeginCalibration();
    }

    if (g_calibrating.load(std::memory_order_relaxed) && g_calibration_slot >= 0) {
      panel.ClearTo(0.0f, 0.0f, 0.0f, 1.0f);
    } else if (g_render_redirect.load(std::memory_order_relaxed)) {
      panel.PrepareUiFrame(g_context);
    } else {
      panel.Clear();
    }

    // Optional, opt-in diagnostic dump of screen names and positions. It is
    // separate from calibration and off by default.
    if (!g_survey_attempted.load(std::memory_order_relaxed) && g_level &&
        (frame % 120) == 0) {
      g_survey_attempted.store(true);
      SurveyUI();
    }
  }
  return o_Present ? o_Present(self, interval, flags) : S_OK;
}

HRESULT __stdcall ResizeBuffersHook(IDXGISwapChain* self, UINT count, UINT width,
                                    UINT height, DXGI_FORMAT format, UINT flags) {
  // Every process swapchain shares this vtable. Only the captured game chain
  // owns the top-screen buffers; panel/probe/overlay chains must pass through
  // untouched or display handling can recurse into the panel's own resize.
  if (self != g_swapchain) {
    return o_ResizeBuffers
        ? o_ResizeBuffers(self, count, width, height, format, flags) : S_OK;
  }
  DIS_INFO("[d3d] the game resized its swapchain to %ux%u", width, height);
  HRESULT hr = o_ResizeBuffers
      ? o_ResizeBuffers(self, count, width, height, format, flags) : S_OK;
  if (g_ready.load(std::memory_order_relaxed)) ThePanel().OnDisplayChanged();
  return hr;
}

// The shared swapchain vtable, read from a throwaway device.
//
// The same technique the Deus Ex mod ended up needing: hooking the factory
// catches only swapchains created through the factory we were handed, and a
// game that reaches its factory through the device does not use ours. Every
// IDXGISwapChain in a process shares one vtable, so patching it catches the
// game's however it was made.
bool HookSwapChainVTable() {
  HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
  if (!d3d11) { DIS_ERR("[d3d] d3d11.dll not loaded"); return false; }
  using CreateFn = HRESULT(__stdcall*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                       const D3D_FEATURE_LEVEL*, UINT, UINT,
                                       const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
                                       ID3D11Device**, D3D_FEATURE_LEVEL*,
                                       ID3D11DeviceContext**);
  auto create = reinterpret_cast<CreateFn>(
      GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain"));
  if (!create) { DIS_ERR("[d3d] no D3D11CreateDeviceAndSwapChain"); return false; }

  WNDCLASSEXW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"DeadIslandSecondScreenProbe";
  RegisterClassExW(&wc);
  HWND probe = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 1, 1,
                               nullptr, nullptr, wc.hInstance, nullptr);
  if (!probe) return false;

  DXGI_SWAP_CHAIN_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.BufferDesc.Width = 1;
  desc.BufferDesc.Height = 1;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 1;
  desc.OutputWindow = probe;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  IDXGISwapChain* chain = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                      D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
  bool ok = false;
  if (SUCCEEDED(hr) && chain) {
    if (void** vt = VTableOf(chain)) {
      Read(vt + kSwapChainPresent, reinterpret_cast<void**>(&o_Present));
      Read(vt + kSwapChainResizeBuffers, reinterpret_cast<void**>(&o_ResizeBuffers));
      ok = g_hooks.Add(vt, kSwapChainPresent, reinterpret_cast<void*>(&PresentHook));
      g_hooks.Add(vt, kSwapChainResizeBuffers, reinterpret_cast<void*>(&ResizeBuffersHook));
      DIS_INFO("[d3d] Present hooked on the shared swapchain vtable %p",
               static_cast<void*>(vt));
    }
  } else {
    DIS_ERR("[d3d] probe device failed 0x%08lX", static_cast<unsigned long>(hr));
  }
  if (context) context->Release();
  if (device) device->Release();
  if (chain) chain->Release();
  DestroyWindow(probe);
  return ok;
}

}  // namespace

void ModOnDxgiUsed() {
  static std::atomic<bool> once{false};
  if (once.exchange(true)) return;
  LogOpen(L"DeadIslandSecondScreen.log");
  Cfg().Load();
  DIS_INFO("Dead Island Definitive Edition -- second screen");
  DIS_INFO("Reads DeadIslandSecondScreen.ini beside the executable. Delete dxgi.dll to remove.");
  if (!Cfg().GetBool("panel.enabled", true)) {
    DIS_INFO("[mod] disabled by panel.enabled=0; leaving the game untouched");
    return;
  }
  // Install before the first Present. Waiting for Present misses lifecycle
  // queries made while the title screen and initial world are constructed.
  InstallLevelHooks();
  if (!HookSwapChainVTable()) {
    DIS_ERR("[mod] without a Present hook nothing further can run; standing down");
  }
}

void ModShutdown() {
  StopTouchInput();
  ThePanel().Destroy();
  g_hooks.RemoveAll();
  g_probe_hooks.RemoveAll();
  g_trial_hooks.RemoveAll();
  DIS_INFO("[mod] unloaded cleanly");
  LogClose();
}

}  // namespace dis

#include "CharacterRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "GameApi.h"
#include "IatHook.h"
#include "Log.h"
#include "Panel.h"
#include "Snapshot.h"
#include "StateStore.h"

#if defined(__GNUC__)
#define GD_FASTCALL __attribute__((fastcall))
#else
#define GD_FASTCALL __fastcall
#endif

namespace gdds {
namespace {

constexpr char kEnginePresentName[] =
    "?PresentSurface@Engine@GAME@@QAEXXZ";
constexpr DWORD kMinimumFrameIntervalMs = 33;

struct Vec3 {
  float x;
  float y;
  float z;
};

struct Coords {
  Vec3 axis_x;
  Vec3 axis_y;
  Vec3 axis_z;
  Vec3 origin;
};
static_assert(sizeof(Coords) == 48, "Grim Dawn I386 Coords ABI changed");

struct WorldVec3 {
  void* region;
  float x;
  float y;
  float z;
};
static_assert(sizeof(WorldVec3) == 16,
              "Grim Dawn I386 WorldVec3 ABI changed");

struct Viewport {
  int x;
  int y;
  int width;
  int height;
};
static_assert(sizeof(Viewport) == 16,
              "Grim Dawn I386 Viewport ABI changed");

struct Rect {
  float x;
  float y;
  float width;
  float height;
};

struct Color {
  float red;
  float green;
  float blue;
  float alpha;
};

struct RendererApi {
  uintptr_t engine_present = 0;
  uintptr_t get_graphics_engine = 0;
  uintptr_t get_canvas = 0;
  uintptr_t get_render_device = 0;
  uintptr_t get_scene_renderer = 0;
  uintptr_t get_color_target = 0;
  uintptr_t get_depth_target = 0;
  uintptr_t get_viewport = 0;
  uintptr_t clear_render_surface = 0;
  uintptr_t set_target_surface = 0;
  uintptr_t set_render_target = 0;
  uintptr_t set_depth_target = 0;
  uintptr_t set_viewport = 0;
  uintptr_t canvas_present_surface = 0;
  uintptr_t render_rect = 0;
  uintptr_t get_custom_scene = 0;
  uintptr_t render_custom_scene = 0;
  uintptr_t camera_ctor = 0;
  uintptr_t camera_set_coords = 0;
  uintptr_t camera_set_fov = 0;
  uintptr_t camera_set_near = 0;
  uintptr_t camera_set_far = 0;
  uintptr_t camera_get_frustum = 0;
  uintptr_t hwnd_window_ctor = 0;
  uintptr_t hwnd_window_dtor = 0;
  uintptr_t destroy_surface = 0;
  uintptr_t fill_device_settings = 0;
  uintptr_t get_path_position = 0;
  uintptr_t create_secondary_surface = 0;
  uintptr_t create_texture = 0;
  uintptr_t destroy_texture = 0;
};

RendererApi g_api;
HMODULE g_engine_module = nullptr;
HMODULE g_game_module = nullptr;
HMODULE g_direct3d_module = nullptr;
void* g_secondary_surface = nullptr;
void* g_panel_texture = nullptr;
void* g_render_device = nullptr;
uint32_t* g_panel_pixels = nullptr;
size_t g_panel_pixel_capacity = 0;
uint32_t g_uploaded_revision = 0;
int g_surface_width = 0;
int g_surface_height = 0;
HWND g_surface_window = nullptr;
volatile LONG g_present_depth = 0;
volatile LONG g_render_thread_id = 0;
volatile LONG g_renderer_faulted = 0;
ULONGLONG g_last_render_ms = 0;
void** g_present_hook_slot = nullptr;

extern "C" void GddsCallThisVoidNoArgs(uintptr_t function, void* self);
extern "C" void* GddsCallThisPointerNoArgs(uintptr_t function,
                                             const void* self);
extern "C" void* GddsCallThisPointer1(uintptr_t function, void* self,
                                        uint32_t argument);
extern "C" void* GddsCallThisPointer2(uintptr_t function, void* self,
                                        uint32_t argument1,
                                        uint32_t argument2);
extern "C" void* GddsCallThisPointer5(uintptr_t function, void* self,
                                        uint32_t argument1,
                                        uint32_t argument2,
                                        uint32_t argument3,
                                        uint32_t argument4,
                                        uint32_t argument5);
extern "C" void GddsCallThisVoid1(uintptr_t function, void* self,
                                    uint32_t argument);
extern "C" void GddsCallThisVoid2(uintptr_t function, void* self,
                                    uint32_t argument1,
                                    uint32_t argument2);
extern "C" void GddsCallThisVoid3(uintptr_t function, void* self,
                                    uint32_t argument1,
                                    uint32_t argument2,
                                    uint32_t argument3);
extern "C" void GddsCallThisVoid4(uintptr_t function, void* self,
                                    uint32_t argument1,
                                    uint32_t argument2,
                                    uint32_t argument3,
                                    uint32_t argument4);
extern "C" void GddsCallThisWorldVec(uintptr_t function, const void* self,
                                       WorldVec3* output);
extern "C" void GddsCallThisFrustum(uintptr_t function, const void* self,
                                      void* output,
                                      const Viewport* viewport);

uint32_t PointerBits(const void* value) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));
}

uint32_t FloatBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

bool Readable(const void* address, size_t size) {
  if (!address || !size) return false;
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(address, &info, sizeof(info)) ||
      info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
    return false;
  const uintptr_t start = reinterpret_cast<uintptr_t>(address);
  const uintptr_t base = reinterpret_cast<uintptr_t>(info.BaseAddress);
  return start >= base && size <= info.RegionSize - (start - base);
}

bool Executable(const void* address) {
  if (!address) return false;
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT)
    return false;
  const DWORD protection = info.Protect & 0xffu;
  return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
         protection == PAGE_EXECUTE_READWRITE ||
         protection == PAGE_EXECUTE_WRITECOPY;
}

bool Object(const void* value) {
  if (!Readable(value, sizeof(void*))) return false;
  void* const table = *reinterpret_cast<void* const*>(value);
  return Readable(table, sizeof(void*)) &&
         Executable(*reinterpret_cast<void* const*>(table));
}

bool ObjectOwnedByModule(const void* value, HMODULE expected_module) {
  if (!Object(value) || !expected_module) return false;
  const void* table = *reinterpret_cast<void* const*>(value);
  HMODULE owner = nullptr;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(table), &owner))
    return false;
  return owner == expected_module;
}

uintptr_t ResolveExact(HMODULE module, const char* name, uintptr_t rva) {
  if (!module || !name) return 0;
  const uintptr_t actual =
      reinterpret_cast<uintptr_t>(GetProcAddress(module, name));
  const uintptr_t expected = reinterpret_cast<uintptr_t>(module) + rva;
  if (actual != expected) {
    Log(LogLevel::kError,
        "[RENDER] exact export mismatch name=%s actual=%p expected=%p rva=%08lx",
        name, reinterpret_cast<void*>(actual),
        reinterpret_cast<void*>(expected), static_cast<unsigned long>(rva));
    return 0;
  }
  return actual;
}

Vec3 Subtract(const Vec3& left, const Vec3& right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 Cross(const Vec3& left, const Vec3& right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

Vec3 Normalize(const Vec3& value) {
  const float length =
      std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  if (!std::isfinite(length) || length < 0.0001f) return {0.0f, 0.0f, 0.0f};
  return {value.x / length, value.y / length, value.z / length};
}

bool ResolveDirect3D11() {
  if (g_api.create_secondary_surface && g_api.create_texture &&
      g_api.destroy_texture)
    return true;
  HMODULE module = GetModuleHandleW(L"Direct3D11.dll");
  if (!module) return false;
  const uintptr_t create_secondary = ResolveExact(
      module,
      "?CreateSecondarySurface@Direct3DDevice11@GAME@@UAEPAVRenderSurface@2@PAVEWindow@2@ABURenderDeviceSettings@2@@Z",
      0x00006A70);
  const uintptr_t create_texture = ResolveExact(
      module,
      "?CreateTexture@Direct3DDevice11@GAME@@UAEPAVRenderTexture@2@HHW4RenderTextureFormat@2@PBXW4TextureFlag@2@@Z",
      0x00007B10);
  const uintptr_t destroy_texture = ResolveExact(
      module,
      "?DestroyTexture@Direct3DDevice11@GAME@@UAEXAAPAVRenderTexture@2@@Z",
      0x00007D30);
  if (!create_secondary || !create_texture || !destroy_texture) return false;
  g_direct3d_module = module;
  g_api.create_secondary_surface = create_secondary;
  g_api.create_texture = create_texture;
  g_api.destroy_texture = destroy_texture;
  Log(LogLevel::kInfo,
      "[RENDER] exact Direct3D11 secondary-surface API resolved module=%p",
      module);
  return true;
}

bool EnsurePixelBuffer(size_t required) {
  if (g_panel_pixels && g_panel_pixel_capacity >= required) return true;
  if (!required || required > static_cast<size_t>(8192) * 8192) return false;
  const size_t bytes = required * sizeof(uint32_t);
  void* replacement =
      g_panel_pixels ? HeapReAlloc(GetProcessHeap(), 0, g_panel_pixels, bytes)
                     : HeapAlloc(GetProcessHeap(), 0, bytes);
  if (!replacement) return false;
  g_panel_pixels = static_cast<uint32_t*>(replacement);
  g_panel_pixel_capacity = required;
  return true;
}

void ReleaseRendererResources(void* canvas) {
  SetPanelGpuActive(false);
  if (g_panel_texture && g_render_device && g_api.destroy_texture) {
    void* texture = g_panel_texture;
    GddsCallThisVoid1(g_api.destroy_texture, g_render_device,
                      PointerBits(&texture));
  }
  g_panel_texture = nullptr;
  g_uploaded_revision = 0;
  if (g_secondary_surface && canvas && g_api.destroy_surface)
    GddsCallThisVoid1(g_api.destroy_surface, canvas,
                      PointerBits(g_secondary_surface));
  g_secondary_surface = nullptr;
  g_render_device = nullptr;
  g_surface_width = 0;
  g_surface_height = 0;
  g_surface_window = nullptr;
}

bool EnsureSecondarySurface(void* engine, PanelFrameInfo* frame,
                            void** canvas_output) {
  if (!engine || !frame || !canvas_output) return false;
  void* graphics_engine = GddsCallThisPointerNoArgs(
      g_api.get_graphics_engine, engine);
  if (!Object(graphics_engine)) return false;
  void* canvas =
      GddsCallThisPointerNoArgs(g_api.get_canvas, graphics_engine);
  if (!Object(canvas)) return false;
  *canvas_output = canvas;
  if (!ResolveDirect3D11()) return false;
  void* render_device =
      GddsCallThisPointerNoArgs(g_api.get_render_device, canvas);
  if (!ObjectOwnedByModule(render_device, g_direct3d_module)) {
    LogRateLimited("render-backend", 5000, LogLevel::kError,
                   "[RENDER] active RenderDevice is not owned by the validated Direct3D11 backend device=%p module=%p",
                   render_device, g_direct3d_module);
    SetPanelGpuActive(false);
    return false;
  }
  if (g_secondary_surface && render_device != g_render_device) {
    LogRateLimited("render-device-reset", 5000, LogLevel::kWarning,
                   "[RENDER] RenderDevice changed; stale secondary resources discarded and recreation scheduled");
    // Device-loss makes the old device-owned objects unsafe to destroy. Drop
    // the stale handles and recreate against the validated replacement.
    g_panel_texture = nullptr;
    g_secondary_surface = nullptr;
    g_render_device = nullptr;
    g_surface_width = g_surface_height = 0;
    g_surface_window = nullptr;
    SetPanelGpuActive(false);
  }
  if (g_secondary_surface && frame->window == g_surface_window &&
      frame->width == g_surface_width && frame->height == g_surface_height)
    return true;
  if (g_secondary_surface) ReleaseRendererResources(canvas);

  alignas(4) unsigned char window_object[8] = {};
  GddsCallThisPointer1(g_api.hwnd_window_ctor, window_object,
                       PointerBits(frame->window));
  alignas(4) unsigned char settings[0x28] = {};
  GddsCallThisVoid1(g_api.fill_device_settings, graphics_engine,
                    PointerBits(settings));
  *reinterpret_cast<int*>(settings + 0x04) = frame->width;
  *reinterpret_cast<int*>(settings + 0x08) = frame->height;
  if (*reinterpret_cast<uint32_t*>(settings + 0x18) == 0)
    *reinterpret_cast<uint32_t*>(settings + 0x18) = 1;
  void* surface = GddsCallThisPointer2(
      g_api.create_secondary_surface, render_device,
      PointerBits(window_object), PointerBits(settings));
  GddsCallThisVoidNoArgs(g_api.hwnd_window_dtor, window_object);
  if (!Object(surface)) {
    LogRateLimited("render-surface-create", 5000, LogLevel::kError,
                   "[RENDER] secondary surface creation failed hwnd=%p size=%dx%d",
                   frame->window, frame->width, frame->height);
    return false;
  }
  g_secondary_surface = surface;
  g_render_device = render_device;
  g_surface_width = frame->width;
  g_surface_height = frame->height;
  g_surface_window = frame->window;
  Log(LogLevel::kInfo,
      "[RENDER] secondary swapchain created hwnd=%p surface=%p size=%dx%d thread=%lu",
      frame->window, surface, frame->width, frame->height,
      static_cast<unsigned long>(GetCurrentThreadId()));
  return true;
}

bool EnsurePanelTexture(PanelFrameInfo* frame) {
  if (!frame || !g_render_device) return false;
  PanelFrameInfo queried = {};
  if (!CopyPanelFrame(nullptr, 0, &queried) || !queried.window ||
      queried.width <= 0 || queried.height <= 0)
    return false;
  const size_t required = static_cast<size_t>(queried.width) * queried.height;
  if (g_panel_texture && queried.revision == g_uploaded_revision) {
    *frame = queried;
    return true;
  }
  if (!EnsurePixelBuffer(required) ||
      !CopyPanelFrame(g_panel_pixels, g_panel_pixel_capacity, &queried))
    return false;
  void* texture = GddsCallThisPointer5(
      g_api.create_texture, g_render_device,
      static_cast<uint32_t>(queried.width),
      static_cast<uint32_t>(queried.height), 2u,
      PointerBits(g_panel_pixels), 0u);
  if (!Object(texture)) {
    LogRateLimited("render-ui-texture", 5000, LogLevel::kError,
                   "[RENDER] panel texture upload failed revision=%u size=%dx%d",
                   queried.revision, queried.width, queried.height);
    return false;
  }
  if (g_panel_texture) {
    void* previous = g_panel_texture;
    GddsCallThisVoid1(g_api.destroy_texture, g_render_device,
                      PointerBits(&previous));
  }
  g_panel_texture = texture;
  g_uploaded_revision = queried.revision;
  *frame = queried;
  return true;
}

bool GetPlayerPosition(void* player, WorldVec3* position) {
  if (!Object(player) || !position) return false;
  *position = {};
  GddsCallThisWorldVec(g_api.get_path_position, player, position);
  return Object(position->region) && std::isfinite(position->x) &&
         std::isfinite(position->y) && std::isfinite(position->z) &&
         std::fabs(position->x) < 10000000.0f &&
         std::fabs(position->y) < 10000000.0f &&
         std::fabs(position->z) < 10000000.0f;
}

bool RenderLiveCharacter(void* canvas, const PanelFrameInfo& frame) {
  if (!frame.character_stage) return false;
  Snapshot snapshot = {};
  if (!CopyLatestSnapshot(&snapshot) ||
      snapshot.lifecycle != Lifecycle::kLive)
    return false;
  void* player = ValidatedRenderPlayer();
  WorldVec3 position = {};
  if (!GetPlayerPosition(player, &position)) return false;

  const int width = frame.character_bounds.right - frame.character_bounds.left;
  const int height = frame.character_bounds.bottom - frame.character_bounds.top;
  if (width < 32 || height < 32) return false;
  const Viewport viewport = {frame.character_bounds.left,
                             frame.character_bounds.top, width, height};

  alignas(16) unsigned char camera[0x4C] = {};
  GddsCallThisPointerNoArgs(g_api.camera_ctor, camera);
  const Vec3 target = {position.x, position.y, position.z + 1.05f};
  const Vec3 source = {position.x, position.y - 4.0f,
                       position.z + 1.15f};
  const Vec3 forward = Normalize(Subtract(target, source));
  const Vec3 right = Normalize(Cross({0.0f, 0.0f, 1.0f}, forward));
  const Vec3 up = Normalize(Cross(forward, right));
  if (forward.x == 0.0f && forward.y == 0.0f && forward.z == 0.0f)
    return false;
  const Coords coords = {right, up, forward, source};
  GddsCallThisVoid1(g_api.camera_set_coords, camera, PointerBits(&coords));
  GddsCallThisVoid1(g_api.camera_set_fov, camera, FloatBits(0.58f));
  GddsCallThisVoid1(g_api.camera_set_near, camera, FloatBits(0.05f));
  GddsCallThisVoid1(g_api.camera_set_far, camera, FloatBits(50.0f));

  alignas(16) unsigned char frustum[0x410] = {};
  GddsCallThisFrustum(g_api.camera_get_frustum, camera, frustum, &viewport);
  void* renderer = GddsCallThisPointer2(g_api.get_scene_renderer, canvas, 0, 0);
  if (!Object(renderer)) return false;
  void* scene = GddsCallThisPointer5(
      g_api.get_custom_scene, renderer, PointerBits(camera),
      PointerBits(position.region), PointerBits(&viewport),
      PointerBits(frustum), 0x618u);
  if (!Object(scene)) return false;
  const uintptr_t* player_vtable =
      *reinterpret_cast<const uintptr_t* const*>(player);
  if (!Readable(player_vtable, 0x60)) return false;
  const uintptr_t add_attached = player_vtable[0x54 / sizeof(uintptr_t)];
  const uintptr_t add_to_scene = player_vtable[0x5C / sizeof(uintptr_t)];
  const uintptr_t expected_attached =
      reinterpret_cast<uintptr_t>(g_game_module) + 0x0005D520;
  const uintptr_t expected_add_to_scene =
      reinterpret_cast<uintptr_t>(g_game_module) + 0x0032E580;
  if (add_attached != expected_attached ||
      add_to_scene != expected_add_to_scene ||
      !Executable(reinterpret_cast<void*>(add_attached)) ||
      !Executable(reinterpret_cast<void*>(add_to_scene))) {
    LogRateLimited("render-player-vtable", 3000, LogLevel::kError,
                   "[RENDER] exact Player scene vtable validation failed attached=%p/%p add=%p/%p",
                   reinterpret_cast<void*>(add_attached),
                   reinterpret_cast<void*>(expected_attached),
                   reinterpret_cast<void*>(add_to_scene),
                   reinterpret_cast<void*>(expected_add_to_scene));
    return false;
  }
  struct CustomCharacterSubmissionGuard {
    CustomCharacterSubmissionGuard() { BeginCustomCharacterSubmission(); }
    ~CustomCharacterSubmissionGuard() { EndCustomCharacterSubmission(); }
  } custom_character_submission_guard;
  GddsCallThisVoid2(add_to_scene, player, PointerBits(scene),
                    PointerBits(frustum));
  GddsCallThisVoid2(add_attached, player, PointerBits(scene),
                    PointerBits(frustum));
  GddsCallThisVoid1(g_api.render_custom_scene, renderer, PointerBits(&scene));
  return true;
}

void RenderBottom(void* engine) {
  if (InterlockedCompareExchange(&g_renderer_faulted, 0, 0) != 0) {
    SetPanelGpuActive(false);
    return;
  }
  PanelFrameInfo frame = {};
  if (!CopyPanelFrame(nullptr, 0, &frame) || !frame.window) {
    SetPanelGpuActive(false);
    return;
  }
  void* canvas = nullptr;
  if (!EnsureSecondarySurface(engine, &frame, &canvas) ||
      !EnsurePanelTexture(&frame)) {
    SetPanelGpuActive(false);
    return;
  }
  if (!IsWindow(frame.window) || frame.window != g_surface_window ||
      frame.width != g_surface_width || frame.height != g_surface_height) {
    LogRateLimited("render-panel-changed", 3000, LogLevel::kWarning,
                   "[RENDER] panel identity or size changed during frame assembly; rebuilding resources");
    ReleaseRendererResources(canvas);
    return;
  }

  void* old_color =
      GddsCallThisPointerNoArgs(g_api.get_color_target, canvas);
  void* old_depth =
      GddsCallThisPointerNoArgs(g_api.get_depth_target, canvas);
  const Viewport* old_viewport_pointer = static_cast<const Viewport*>(
      GddsCallThisPointerNoArgs(g_api.get_viewport, canvas));
  if (!old_viewport_pointer || !Readable(old_viewport_pointer,
                                         sizeof(Viewport))) {
    SetPanelGpuActive(false);
    return;
  }
  const Viewport old_viewport = *old_viewport_pointer;
  struct CanvasRestore {
    void* canvas;
    void* color;
    void* depth;
    Viewport viewport;
    ~CanvasRestore() {
      GddsCallThisVoid1(g_api.set_target_surface, canvas,
                        PointerBits(color));
      GddsCallThisVoid2(g_api.set_render_target, canvas, 0,
                        PointerBits(color));
      GddsCallThisVoid1(g_api.set_depth_target, canvas, PointerBits(depth));
      GddsCallThisVoid1(g_api.set_viewport, canvas, PointerBits(&viewport));
    }
  } restore = {canvas, old_color, old_depth, old_viewport};
  const Viewport full_viewport = {0, 0, frame.width, frame.height};
  const Color black = {0.006f, 0.006f, 0.005f, 1.0f};
  const Color white = {1.0f, 1.0f, 1.0f, 1.0f};
  const Rect destination = {0.0f, 0.0f, static_cast<float>(frame.width),
                            static_cast<float>(frame.height)};
  // RenderRect expects source coordinates in texture pixels and normalizes them
  // internally. A 1x1 source stretches the texture's dark top-left texel across
  // the entire second display once the GPU surface takes ownership.
  const Rect source = {0.0f, 0.0f, static_cast<float>(frame.width),
                       static_cast<float>(frame.height)};

  GddsCallThisVoid1(g_api.set_target_surface, canvas,
                    PointerBits(g_secondary_surface));
  GddsCallThisVoid2(g_api.set_render_target, canvas, 0,
                    PointerBits(g_secondary_surface));
  GddsCallThisVoid1(g_api.set_viewport, canvas, PointerBits(&full_viewport));
  GddsCallThisVoid3(g_api.clear_render_surface, canvas,
                    PointerBits(g_secondary_surface), PointerBits(&black), 7u);
  const bool drew_character = RenderLiveCharacter(canvas, frame);
  GddsCallThisVoid1(g_api.set_viewport, canvas, PointerBits(&full_viewport));
  GddsCallThisVoid4(g_api.render_rect, canvas, PointerBits(&destination),
                    PointerBits(&source), PointerBits(g_panel_texture),
                    PointerBits(&white));
  GddsCallThisVoid1(g_api.canvas_present_surface, canvas,
                    PointerBits(g_secondary_surface));

  SetPanelGpuActive(true);
  if (frame.character_stage && !drew_character)
    LogRateLimited("render-character-wait", 3000, LogLevel::kInfo,
                   "[RENDER] character stage is waiting for a validated live Player and Region");
}

void GD_FASTCALL PresentHook(void* self, void*) {
  const LONG depth = InterlockedIncrement(&g_present_depth);
  if (depth != 1) {
    GddsCallThisVoidNoArgs(g_api.engine_present, self);
    InterlockedDecrement(&g_present_depth);
    return;
  }
  GddsCallThisVoidNoArgs(g_api.engine_present, self);
  const LONG thread_id = static_cast<LONG>(GetCurrentThreadId());
  const LONG owner =
      InterlockedCompareExchange(&g_render_thread_id, thread_id, 0);
  if (owner && owner != thread_id) {
    if (InterlockedCompareExchange(&g_renderer_faulted, 1, 0) == 0)
      Log(LogLevel::kError,
          "[RENDER] fail-closed: PresentSurface thread changed from %lu to %lu",
          static_cast<unsigned long>(owner),
          static_cast<unsigned long>(thread_id));
    SetPanelGpuActive(false);
    InterlockedDecrement(&g_present_depth);
    return;
  }
  // The exact Player is observed while the primary world builds its scene.
  // Validate and publish at the corresponding render tail so activation does
  // not depend on a later GameEngine::Update call.
  SampleAtPrimaryRenderTail();
  const ULONGLONG now = GetTickCount64();
  if (now - g_last_render_ms >= kMinimumFrameIntervalMs) {
    LARGE_INTEGER frequency = {};
    LARGE_INTEGER started = {};
    LARGE_INTEGER finished = {};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    g_last_render_ms = now;
    RenderBottom(self);
    QueryPerformanceCounter(&finished);
    if (frequency.QuadPart > 0) {
      const double milliseconds =
          1000.0 * static_cast<double>(finished.QuadPart - started.QuadPart) /
          static_cast<double>(frequency.QuadPart);
      if (milliseconds > 8.0)
        LogRateLimited("render-bottom-slow", 1000, LogLevel::kWarning,
                       "[PERF] bottom render took %.3f ms", milliseconds);
    }
  }
  InterlockedDecrement(&g_present_depth);
}

}  // namespace

bool InstallCharacterRenderer(HMODULE executable, HMODULE game,
                              HMODULE engine) {
  if (!executable || !game || !engine) return false;
  g_engine_module = engine;
  g_game_module = game;
  g_api.engine_present =
      ResolveExact(engine, kEnginePresentName, 0x00069130);
  g_api.get_graphics_engine = ResolveExact(
      engine, "?GetGraphicsEngine@Engine@GAME@@QBEPAVGraphicsEngine@2@XZ",
      0x0006A130);
  g_api.get_canvas = ResolveExact(
      engine, "?GetCanvas@GraphicsEngine@GAME@@QAEAAVGraphicsCanvas@2@XZ",
      0x00084570);
  g_api.get_render_device = ResolveExact(
      engine, "?GetRenderDevice@GraphicsCanvas@GAME@@QAEPAVRenderDevice@2@XZ",
      0x00020490);
  g_api.get_scene_renderer = ResolveExact(
      engine,
      "?GetSceneRenderer@GraphicsCanvas@GAME@@QBEAAVGraphicsMTRenderer@2@_N0@Z",
      0x000943D0);
  g_api.get_color_target = ResolveExact(
      engine, "?GetColorTarget@GraphicsCanvas@GAME@@QBEPAVRenderSurface@2@XZ",
      0x000839F0);
  g_api.get_depth_target = ResolveExact(
      engine, "?GetDepthTarget@GraphicsCanvas@GAME@@QBEPAVRenderSurface@2@XZ",
      0x00037780);
  g_api.get_viewport = ResolveExact(
      engine, "?GetViewport@GraphicsCanvas@GAME@@QBEABVViewport@2@XZ",
      0x0008A7D0);
  g_api.clear_render_surface = ResolveExact(
      engine,
      "?ClearRenderSurface@GraphicsCanvas@GAME@@QAEXPAVRenderSurface@2@ABVColor@2@I@Z",
      0x0008A800);
  g_api.set_target_surface = ResolveExact(
      engine,
      "?SetTargetSurface@GraphicsCanvas@GAME@@QAEXPAVRenderSurface@2@@Z",
      0x0008A8B0);
  g_api.set_render_target = ResolveExact(
      engine,
      "?SetRenderTarget@GraphicsCanvas@GAME@@QAEXHPAVRenderSurface@2@@Z",
      0x0008A8F0);
  g_api.set_depth_target = ResolveExact(
      engine,
      "?SetDepthTarget@GraphicsCanvas@GAME@@QAEXPAVRenderSurface@2@@Z",
      0x0008A910);
  g_api.set_viewport = ResolveExact(
      engine, "?SetViewport@GraphicsCanvas@GAME@@QAEXABVViewport@2@@Z",
      0x0008A7C0);
  g_api.canvas_present_surface = ResolveExact(
      engine,
      "?PresentSurface@GraphicsCanvas@GAME@@QAEXPAVRenderSurface@2@@Z",
      0x0008A7B0);
  g_api.render_rect = ResolveExact(
      engine,
      "?RenderRect@GraphicsCanvas@GAME@@QAEXABVRect@2@0PBVRenderTexture@2@ABVColor@2@@Z",
      0x0008B7B0);
  g_api.get_custom_scene = ResolveExact(
      engine,
      "?GetCustomScene@GraphicsMTRenderer@GAME@@QAEPAVGraphicsScene@2@ABVCamera@2@PAVRegion@2@ABVViewport@2@ABVFrustum@2@W4RenderFlag@2@@Z",
      0x000BF910);
  g_api.render_custom_scene = ResolveExact(
      engine,
      "?RenderCustomScene@GraphicsMTRenderer@GAME@@QAEXAAPAVGraphicsScene@2@@Z",
      0x000BF930);
  g_api.camera_ctor =
      ResolveExact(engine, "??0Camera@GAME@@QAE@XZ", 0x0003C690);
  g_api.camera_set_coords = ResolveExact(
      engine, "?SetCoords@Camera@GAME@@QAEXABVCoords@2@@Z", 0x0003C760);
  g_api.camera_set_fov = ResolveExact(
      engine, "?SetFOV@Camera@GAME@@QAEXM@Z", 0x0003C6F0);
  g_api.camera_set_near = ResolveExact(
      engine, "?SetNearPlane@Camera@GAME@@QAEXM@Z", 0x0003C7B0);
  g_api.camera_set_far = ResolveExact(
      engine, "?SetFarPlane@Camera@GAME@@QAEXM@Z", 0x0003C7D0);
  g_api.camera_get_frustum = ResolveExact(
      engine, "?GetFrustum@Camera@GAME@@QBE?AVFrustum@2@ABVViewport@2@@Z",
      0x0003CE10);
  g_api.hwnd_window_ctor = ResolveExact(
      engine, "??0HwndWindow@GAME@@QAE@QAUHWND__@@@Z", 0x000D2DD0);
  g_api.hwnd_window_dtor =
      ResolveExact(engine, "??1HwndWindow@GAME@@UAE@XZ", 0x00082180);
  g_api.destroy_surface = ResolveExact(
      engine,
      "?DestroySurface@GraphicsCanvas@GAME@@QAEXPAVRenderSurface@2@@Z",
      0x00090690);
  g_api.get_path_position = ResolveExact(
      game, "?GetPathPosition@Character@GAME@@QBE?AVWorldVec3@2@XZ",
      0x00048D80);
  g_api.fill_device_settings =
      reinterpret_cast<uintptr_t>(engine) + 0x00084BB0;

  const uintptr_t* values = reinterpret_cast<const uintptr_t*>(&g_api);
  const size_t engine_value_count =
      offsetof(RendererApi, create_secondary_surface) / sizeof(uintptr_t);
  for (size_t index = 0; index < engine_value_count; ++index) {
    if (!values[index] || !Executable(reinterpret_cast<void*>(values[index]))) {
      Log(LogLevel::kError,
          "[RENDER] exact engine renderer API incomplete index=%u value=%p",
          static_cast<unsigned>(index), reinterpret_cast<void*>(values[index]));
      return false;
    }
  }

  void** slot = FindIatSlot(executable, "Engine.dll", kEnginePresentName);
  void* original = nullptr;
  if (!slot || !InstallIatHook(slot,
                               reinterpret_cast<void*>(g_api.engine_present),
                               reinterpret_cast<void*>(&PresentHook),
                               &original)) {
    Log(LogLevel::kError,
        "[RENDER] Engine::PresentSurface hook installation refused slot=%p expected=%p",
        static_cast<void*>(slot),
        reinterpret_cast<void*>(g_api.engine_present));
    return false;
  }
  Log(LogLevel::kInfo,
      "[RENDER] render-tail hook installed slot=%p original=%p replacement=%p",
      static_cast<void*>(slot), original, reinterpret_cast<void*>(&PresentHook));
  g_present_hook_slot = slot;
  return true;
}

bool UninstallCharacterRenderer() {
  SetPanelGpuActive(false);
  if (!g_present_hook_slot) return true;
  const bool restored = RestoreIatHook(
      g_present_hook_slot, reinterpret_cast<void*>(&PresentHook),
      reinterpret_cast<void*>(g_api.engine_present));
  g_present_hook_slot = nullptr;
  return restored;
}

}  // namespace gdds

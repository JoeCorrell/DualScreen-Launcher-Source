#include "Scaleform.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

#include "Config.h"
#include "Hook.h"
#include "Log.h"
#include "Mem.h"
#include "Panel.h"
#include "Rtti.h"

#define DXS_THISCALL __attribute__((__fastcall__))

// The display seam's state and stub, at file scope.
//
// Not inside the anonymous namespace: these are referenced by hand-written
// assembly under their C symbol names, and by a member function defined outside
// that namespace. Anonymous-namespace linkage left the assembler's symbol
// unresolvable at link time.
extern "C" {
void* dxs_current_movie = nullptr;
void* dxs_display_orig = nullptr;
unsigned dxs_display_count = 0;
volatile unsigned dxs_suppress_display = 0;
unsigned dxs_suppressed_display_count = 0;
void dxs_display_stub(void);
}
asm(".globl _dxs_display_stub\n"
    "_dxs_display_stub:\n"
    "  movl %ecx, _dxs_current_movie\n"
    "  incl _dxs_display_count\n"
    // Scaleform consumes the GViewport before the queued D3D work is emitted,
    // then rebuilds its raster state later. A zero scissor in BeginDisplay
    // therefore does not reliably hide DXHR's anonymous gameplay root under
    // DXVK. Suppress the root at its actual Display entry instead. This stub
    // still has no call or stack adjustment, so the unknown forwarding ABI is
    // preserved exactly; the no-argument Display method returns directly when
    // gameplay owns the screen and tail-jumps to retail in every other state.
    "  cmpl $0, _dxs_suppress_display\n"
    "  je 1f\n"
    "  incl _dxs_suppressed_display_count\n"
    "  ret\n"
    "1:\n"
    "  jmp  *_dxs_display_orig\n");

namespace dxs {
namespace {

ScaleformBridge g_bridge;

// ---------------------------------------------------------------- shape
// Where slot 12 keeps the placement rectangle. Defaults from static analysis of
// the shipped Director's Cut; overridable, because a patched build that moved
// them should be a line in the ini rather than a new DLL.
struct MovieShape {
  uint32_t placement_slot = 12;
  uint32_t descriptor = 0x08;
  uint32_t rect_x = 0x30;
  uint32_t rect_y = 0x34;
  uint32_t rect_w = 0x38;
  uint32_t rect_h = 0x3C;
};
MovieShape g_shape;

// ScaleformMovieInstance slot 26 forwards straight to GFxMovieView::Display:
//
//   mov ecx, [ecx + 0x4c]   ; this->movieview
//   mov eax, [ecx]
//   mov eax, [eax + 0xb4]   ; movieview vtable slot 45
//   jmp eax                 ; tail call
//
// It runs once per visible element per frame -- 1252 calls in 600 frames while
// BeginDisplay ran 1865, so each display opens one or two passes -- and its
// `this` is the game's own movie instance. That last part is what makes it the
// seam: BeginDisplay knows the renderer but not the movie, and slot 12, which
// was supposed to supply the identity, is never called at all in steady state.
constexpr uint32_t kMovieDisplaySlot = 26;
constexpr uint32_t kBeginDisplaySlot = 12;
// Slot 13, immediately after BeginDisplay and 17 bytes long, forwarding to the
// renderer's own slot 5: EndDisplay. Hooking it is what stops the panel's
// render target outliving the pass it was bound for -- without it the binding
// survives into the next frame's scene geometry, and the bottom screen shows
// the camera view instead of the interface.
constexpr uint32_t kEndDisplaySlot = 13;
constexpr uint32_t kApplySlot = 4;

// GViewport, GFx 3.x, as dwords. Index 10 read back as 0x3F800000 -- 1.0f --
// which is what pins the layout: it is Scale, not Flags.
enum ViewportField {
  kVpBufferWidth = 0, kVpBufferHeight = 1,
  kVpLeft = 2, kVpTop = 3, kVpWidth = 4, kVpHeight = 5,
  kVpScissorLeft = 6, kVpScissorTop = 7, kVpScissorWidth = 8, kVpScissorHeight = 9,
  kVpScale = 10, kVpAspect = 11, kVpFlags = 12,
  kVpDwords = 13,
};
// ---------------------------------------------------------------- originals
using ThisNoArgFn = void(DXS_THISCALL*)(void*, void*);
using BeginDisplayFn = void(DXS_THISCALL*)(void*, void*, unsigned, void*,
                                           float, float, float, float);
ThisNoArgFn o_Placement = nullptr;
ThisNoArgFn o_EndDisplay = nullptr;
BeginDisplayFn o_BeginDisplay = nullptr;
ThisNoArgFn o_Apply = nullptr;

// Keep the borrowed logical placement through the matching EndDisplay so any
// caller-side Scaleform layout/hit-test work sees one coherent pass.
struct ViewportBorrow {
  void* address = nullptr;
  int32_t value[kVpDwords] = {};
  bool restore = false;
};
constexpr int kViewportBorrowDepth = 8;
thread_local ViewportBorrow g_viewport_borrows[kViewportBorrowDepth];
thread_local int g_viewport_borrow_count = 0;
thread_local int g_viewport_borrow_overflow = 0;

RttiIndex* g_rtti = nullptr;

// ---------------------------------------------------------------- state
std::atomic<uint32_t> g_frame{0};
std::atomic<uint32_t> g_movies_this_frame{0};
std::atomic<uint32_t> g_begins_this_frame{0};
std::atomic<uint32_t> g_frames_sampled{0};
std::atomic<uint32_t> g_movies_sampled{0};
std::atomic<uint32_t> g_begins_sampled{0};

// Cumulative, never reset. The per-frame counters answer "what is the ratio";
// these answer the prior question of whether a seam is reached at all, which is
// the one that matters when the panel comes up empty.
std::atomic<uint32_t> g_total_movies{0};
std::atomic<uint32_t> g_total_begins{0};
std::atomic<uint32_t> g_total_applies{0};
std::atomic<uint32_t> g_total_ends{0};
std::atomic<uint32_t> g_displays_this_frame{0};
PassMode g_mode = PassMode::kUnknown;
bool g_enabled = true;
bool g_route_all = false;
bool g_native_interception = false;

std::mutex g_placed_lock;
std::vector<PlacedMovie> g_placed;
std::vector<PlacedMovie> g_placed_last;
std::atomic<int> g_pages{0};

// Cache of controller name per movie instance. The scan that finds an owner
// walks up to thirty-three pointers and does an RTTI lookup on each, which is
// far too much to repeat for every element of the HUD every frame.
std::mutex g_owner_lock;
std::unordered_map<void*, std::string> g_owner_cache;

// A census of what actually displays, kept cumulatively.
//
// "How many did we route this frame" turned out to answer 0 almost always,
// because the sample it read is only filled every sixty-fourth frame. That is
// the wrong question anyway: when the panel is empty the thing worth knowing is
// which elements the game is drawing at all and whether they can be named,
// because an element nobody can name falls through to the main screen and looks
// identical to an element deliberately left there.
struct Census { uint32_t displayed; uint32_t routed; };
std::mutex g_census_lock;
std::map<std::string, Census> g_census;
std::atomic<uint32_t> g_routed_total{0};
std::atomic<uint32_t> g_unnamed_total{0};
std::atomic<uint32_t> g_null_movie_total{0};

void NoteCensus(const std::string& name, bool routed) {
  std::lock_guard<std::mutex> guard(g_census_lock);
  Census& c = g_census[name];
  c.displayed++;
  if (routed) c.routed++;
}

void ReportCensus() {
  std::lock_guard<std::mutex> guard(g_census_lock);
  DXS_INFO("[sf] census: %u displays named, %u could not be named, %u had no instance;"
           " %u routed to the panel in total",
           static_cast<unsigned>(g_census.size()), g_unnamed_total.load(),
           g_null_movie_total.load(), g_routed_total.load());
  for (const auto& kv : g_census) {
    DXS_INFO("[sf]   %-46s displayed=%-6u routed=%u", kv.first.c_str(),
             kv.second.displayed, kv.second.routed);
  }
}

// A one-off field map of a movie instance.
//
// OwnerOf looks for a back-pointer to an Ns*MovieController and the panel came
// up empty during actual gameplay, so that assumption is wrong: the controller
// very likely owns the instance rather than the other way round, leaving no
// pointer to follow. Rather than guess at another offset, this prints what is
// actually in the object -- every word in the first 0x90 bytes, resolved as an
// RTTI class if it points at one, as text if it points at a string, and as a
// float when the bits look like a plausible coordinate.
//
// Printed for the first few distinct instances only. It is a map to read once,
// not telemetry.
std::atomic<int> g_described{0};
std::mutex g_seen_lock;
std::map<void*, uint32_t> g_seen_instances;

// How many distinct movie instances exist, which is the question that decides
// the whole architecture. The first version of this described the first six
// *calls* rather than the first six *instances*, and they all landed in the
// same 30ms of startup -- so it reported one instance six times and looked
// exactly like "there is only one movie". Counting distinct pointers is the
// difference between a measurement and a coincidence.
bool NoteInstance(void* movie) {
  std::lock_guard<std::mutex> guard(g_seen_lock);
  auto it = g_seen_instances.find(movie);
  if (it != g_seen_instances.end()) { it->second++; return false; }
  g_seen_instances.emplace(movie, 1u);
  return true;
}

void ReportInstances() {
  std::lock_guard<std::mutex> guard(g_seen_lock);
  DXS_INFO("[sf] %u distinct ScaleformMovieInstance object(s) have displayed:",
           static_cast<unsigned>(g_seen_instances.size()));
  for (const auto& kv : g_seen_instances) {
    DXS_INFO("[sf]   %p  displayed %u times", kv.first, kv.second);
  }
}

void DescribeInstance(void* movie) {
  if (!NoteInstance(movie)) return;
  if (g_described.fetch_add(1, std::memory_order_relaxed) >= 8) return;

  const char* self_raw = g_rtti ? g_rtti->ClassOf(movie) : nullptr;
  DXS_INFO("[map] --- instance %p is %s ---", movie,
           self_raw ? (self_raw[0] == '.' ? Demangle(self_raw).c_str() : self_raw)
                    : "<unnamed>");
  for (size_t off = 0; off < 0x90; off += 4) {
    void* value = nullptr;
    if (!Read(static_cast<uint8_t*>(movie) + off, &value)) continue;
    if (!value) continue;

    const char* cls = g_rtti ? g_rtti->ClassOf(value) : nullptr;
    if (cls) {
      std::string n = cls[0] == '.' ? Demangle(cls) : std::string(cls);
      DXS_INFO("[map]   +0x%02X = %p  object: %s", static_cast<unsigned>(off), value,
               n.c_str());
      continue;
    }
    if (const char* text = SafeCStr(value, 96)) {
      bool printable = *text != 0;
      for (const char* c = text; *c; ++c) {
        if (*c < 0x20 || *c > 0x7e) { printable = false; break; }
      }
      if (printable) {
        DXS_INFO("[map]   +0x%02X = %p  text: \"%s\"", static_cast<unsigned>(off),
                 value, text);
        continue;
      }
    }
    float f = 0;
    Read(static_cast<uint8_t*>(movie) + off, &f);
    if (f > -100000.0f && f < 100000.0f && (f > 0.0001f || f < -0.0001f)) {
      DXS_INFO("[map]   +0x%02X = 0x%08X  (%.2f as float)", static_cast<unsigned>(off),
               static_cast<unsigned>(reinterpret_cast<uintptr_t>(value)), f);
    } else {
      DXS_INFO("[map]   +0x%02X = %p", static_cast<unsigned>(off), value);
    }
  }
}

// ---------------------------------------------------------------- helpers
std::string OwnerOf(void* movie) {
  {
    std::lock_guard<std::mutex> guard(g_owner_lock);
    auto it = g_owner_cache.find(movie);
    if (it != g_owner_cache.end()) return it->second;
  }
  std::string found;
  for (size_t off = 0; off <= 0x80 && found.empty(); off += 4) {
    void* candidate = PtrAt(movie, off);
    if (!candidate) continue;
    const char* raw = g_rtti ? g_rtti->ClassOf(candidate) : nullptr;
    if (!raw) continue;
    std::string name = raw[0] == '.' ? Demangle(raw) : std::string(raw);
    if (name.find("MovieController") != std::string::npos) found = name;
  }
  std::lock_guard<std::mutex> guard(g_owner_lock);
  auto result = g_owner_cache.emplace(movie, found);
  return result.first->second;
}

bool ReadRect(void* desc, Rect* out) {
  auto* base = static_cast<uint8_t*>(desc);
  return Read(base + g_shape.rect_x, &out->x) && Read(base + g_shape.rect_y, &out->y) &&
         Read(base + g_shape.rect_w, &out->w) && Read(base + g_shape.rect_h, &out->h);
}

bool WriteRect(void* desc, const Rect& r) {
  auto* base = static_cast<uint8_t*>(desc);
  if (!Writable(base + g_shape.rect_x, sizeof(float)) ||
      !Writable(base + g_shape.rect_y, sizeof(float)) ||
      !Writable(base + g_shape.rect_w, sizeof(float)) ||
      !Writable(base + g_shape.rect_h, sizeof(float))) return false;
  *reinterpret_cast<float*>(base + g_shape.rect_x) = r.x;
  *reinterpret_cast<float*>(base + g_shape.rect_y) = r.y;
  *reinterpret_cast<float*>(base + g_shape.rect_w) = r.w;
  *reinterpret_cast<float*>(base + g_shape.rect_h) = r.h;
  return true;
}

// ---------------------------------------------------------------- display seam
//
// A tail jump, not a C++ thunk, and deliberately so.
//
// Slot 26 ends in `jmp eax`, so its argument count is whatever GFxMovieView's
// Display takes -- which is not knowable from the forwarder. A thunk of the
// wrong shape would unbalance the stack on every element of the HUD. This stub
// records `this` out of ECX and jumps straight on: no stack touched, no
// register altered, and `mov` does not disturb flags.
//
// The instance is never cleared, only overwritten. That is correct here: the
// BeginDisplay calls belonging to a display happen inside it, so "the movie
// whose Display was most recently entered" is exactly the right answer for
// every pass that follows, and there is no exit point to clear it from anyway.

// ---------------------------------------------------------------- hooks
void DXS_THISCALL PlacementHook(void* self, void* /*edx*/) {
  g_movies_this_frame.fetch_add(1, std::memory_order_relaxed);
  g_total_movies.fetch_add(1, std::memory_order_relaxed);

  if (!g_enabled || !o_Placement) {
    if (o_Placement) o_Placement(self, nullptr);
    return;
  }

  const std::string owner = OwnerOf(self);
  if (owner.empty()) { o_Placement(self, nullptr); return; }

  const Route route = TheLayout().For(owner);
  if (route.destination == Destination::kMain) { o_Placement(self, nullptr); return; }

  void* desc = PtrAt(self, g_shape.descriptor);
  if (!desc) { o_Placement(self, nullptr); return; }

  Rect original;
  if (!ReadRect(desc, &original)) { o_Placement(self, nullptr); return; }

  if (route.destination == Destination::kHidden) {
    // A zero-sized rectangle rather than skipping the original: the transform
    // still gets written, so anything downstream that reads instance+0x190
    // finds a well-formed matrix instead of last frame's.
    WriteRect(desc, Rect{original.x, original.y, 0.0f, 0.0f});
    o_Placement(self, nullptr);
    WriteRect(desc, original);
    return;
  }

  const Panel& panel = ThePanel();
  Rect target;
  if (route.destination == Destination::kPage) {
    target = TheLayout().Page();
  } else {
    const Rect* zone = TheLayout().Zone(route.zone);
    if (!zone) { o_Placement(self, nullptr); return; }
    // Fitted rather than stretched: the element keeps the aspect it was drawn
    // at and is centred in its zone.
    target = Layout::FitInside(*zone, original.w, original.h);
  }

  WriteRect(desc, target);
  o_Placement(self, nullptr);
  // The rectangle is left as written, not restored: the game hit-tests its own
  // cursor against this same rectangle, so an element that genuinely lives at
  // the new coordinates needs no input translation at all. Restoring it is what
  // would force a bridge to exist.

  if ((g_frame.load(std::memory_order_relaxed) & 0x3) == 0) {
    PlacedMovie placed;
    placed.movie = self;
    placed.controller = owner;
    placed.source = original;
    placed.target = target;
    placed.interactive = route.interactive;
    placed.page = route.destination == Destination::kPage;
    placed.frame = g_frame.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> guard(g_placed_lock);
    g_placed.push_back(placed);
    if (placed.page) g_pages.fetch_add(1, std::memory_order_relaxed);
  }
  (void)panel;
}

// Who calls BeginDisplay.
//
// This is the question the slot survey left open. Display passes are per movie
// -- roughly three a frame against five advances -- so the seam exists, but with
// slot 12 never firing there is nothing saying which movie a pass belongs to.
// The caller's return address names the function that displays a movie, and
// that function has the instance as `this`. Measuring it replaces another round
// of guessing at vtable indices.
constexpr int kMaxCallers = 16;
struct CallerSlot { uintptr_t site; uint32_t count; };
CallerSlot g_callers[kMaxCallers];
std::atomic<int> g_caller_count{0};

void NoteCaller(uintptr_t site) {
  const int n = g_caller_count.load(std::memory_order_acquire);
  for (int i = 0; i < n && i < kMaxCallers; ++i) {
    if (g_callers[i].site == site) { g_callers[i].count++; return; }
  }
  if (n >= kMaxCallers) return;
  const int slot = g_caller_count.fetch_add(1, std::memory_order_acq_rel);
  if (slot >= kMaxCallers) return;
  g_callers[slot].site = site;
  g_callers[slot].count = 1;
}

// Decides the route for the movie currently displaying. In the supported
// whole-interface mode it clips the legacy gameplay root; the panel is drawn
// independently by the custom renderer.
//
// The viewport is the right lever rather than the descriptor rectangle: it is
// the destination rectangle inside the render target and is passed in on every
// pass. It is borrowed through the matching EndDisplay and then restored. The
// descriptor rectangle is written once at setup and read by hit-testing;
// leaving it alone is what keeps the game's own input handling coherent.
bool RouteCurrentPass(void* viewport) {
  if (!g_enabled || !viewport) return false;

  void* movie = dxs_current_movie;
  if (!movie) { g_null_movie_total.fetch_add(1, std::memory_order_relaxed); return false; }
  DescribeInstance(movie);

  const std::string owner = OwnerOf(movie);

  // The auxiliary window is created early for DXGI, but showing it during the
  // publisher logos/title/load flow obscures the screen on a one-monitor test
  // setup and exposes an unfinished dashboard on Thor. These named native
  // movies are authoritative state evidence; no timer or guessed frame count.
  if (owner == "NsMainMenuMovieController" ||
      owner == "NsLoadingMovieController" ||
      owner == "NsSaveLoadMovieController" ||
      owner == "NsGameIntroMovieController") {
    ThePanel().SetGameLoaded(false, owner.c_str());
  }
  // Gameplay HUD movies are created while a save is still streaming, before
  // Jensen's inventory/player graph is necessarily coherent. They are useful
  // routing evidence but are not a safe load-complete signal. The Present
  // thread's stable native-inventory probe is the only path that opens the
  // lower UI, preventing these early movies from bypassing the save-load gate.

  // While the lower output carries its blank claim frame, title/loading UI
  // must continue through the original renderer onto the top display. Without
  // this guard the anonymous whole-ui root is redirected into an unpublished
  // lower back buffer and the main menu appears to vanish.
  if (!ThePanel().game_loaded()) {
    NoteCensus(owner.empty() ? std::string("<pre-game root>") : owner, false);
    return false;
  }

  // DXHRDC submits its visible Scaleform tree as one root. During normal
  // gameplay that root is the legacy HUD we replace below. A genuine modal
  // screen (pause, hacking, dialogue choice, vendor, or a controller-opened
  // native menu) owns the game's menu-input lease, though, and hiding that root
  // would make the interaction invisible on both displays. Custom pages never
  // acquire this lease, so it is an exact distinction rather than a timer or a
  // guessed controller name. Leave modal Scaleform on the upper display.
  if (ThePanel().menu_input_locked()) {
    NoteCensus(owner.empty() ? std::string("<modal root>") : owner, false);
    return false;
  }

  // The reticle is world feedback rather than dashboard information. Keep
  // this one named movie on the upper display while the health, energy,
  // weapon, radar, quickbar, objective and notification HUD movies are
  // replaced by the custom lower-screen interface.
  if (owner == "NsReticleMovieController") {
    NoteCensus(owner, false);
    return false;
  }

  // Replace everything in the gameplay root, named or not. Modal UI returned
  // above remains fully vanilla and visible on the upper display.
  if (g_route_all) {
    if (!ThePanel().ready() ||
        !Writable(viewport, sizeof(int32_t) * kVpDwords)) return false;
    auto* v = static_cast<int32_t*>(viewport);
    // The lower panel is an opaque, fully custom interface. Preserve the
    // projection and buffer dimensions Scaleform uses for layout/hit testing,
    // but give the gameplay root an empty raster scissor for this display
    // pass. BeginDisplay consumes these fields while building the queued
    // commands; EndDisplay restores the borrowed GViewport below. This hides
    // the legacy HUD without detouring the engine's high-volume primitive
    // submit/queue/execute path or rewriting four D3D callsites.
    v[kVpScissorLeft] = 0;
    v[kVpScissorTop] = 0;
    v[kVpScissorWidth] = 0;
    v[kVpScissorHeight] = 0;
    g_routed_total.fetch_add(1, std::memory_order_relaxed);
    NoteCensus(owner.empty() ? std::string("<unnamed>") : owner, true);
    return true;
  }

  if (owner.empty()) {
    g_unnamed_total.fetch_add(1, std::memory_order_relaxed);
    // Named by the instance's own class instead, so the census still says what
    // was on screen even when the owning controller cannot be found.
    const char* raw = g_rtti ? g_rtti->ClassOf(movie) : nullptr;
    NoteCensus(raw ? (raw[0] == '.' ? "?owner of " + Demangle(raw) : std::string("?owner of ") + raw)
                   : "?unidentifiable", false);
    return true;
  }

  const Route route = TheLayout().For(owner);
  if (route.destination == Destination::kMain) { NoteCensus(owner, false); return false; }

  Panel& panel = ThePanel();
  if (!panel.ready()) return false;
  if (!Writable(viewport, sizeof(int32_t) * kVpDwords)) return false;
  auto* v = static_cast<int32_t*>(viewport);

  if (route.destination == Destination::kHidden) {
    // A zero projection extent makes GFx divide by zero. Keep the viewport
    // well-formed and suppress rasterization with an empty scissor instead.
    v[kVpScissorWidth] = 0;
    v[kVpScissorHeight] = 0;
    // Report the mutation so BeginDisplay records this borrowed viewport and
    // EndDisplay restores it. Returning false here used to leave the shared
    // GViewport clipped after the hidden pass.
    return true;
  }

  const Rect source{static_cast<float>(v[kVpLeft]), static_cast<float>(v[kVpTop]),
                    static_cast<float>(v[kVpWidth]), static_cast<float>(v[kVpHeight])};
  Rect target;
  if (route.destination == Destination::kPage) {
    target = TheLayout().Page();
  } else {
    const Rect* zone = TheLayout().Zone(route.zone);
    if (!zone) return false;
    // Fitted, not stretched: the element keeps the aspect it was drawn at.
    target = Layout::FitInside(*zone, source.w > 0 ? source.w : 16.0f,
                               source.h > 0 ? source.h : 9.0f);
  }

  v[kVpBufferWidth] = panel.width();
  v[kVpBufferHeight] = panel.height();
  v[kVpLeft] = static_cast<int32_t>(target.x);
  v[kVpTop] = static_cast<int32_t>(target.y);
  v[kVpWidth] = static_cast<int32_t>(target.w);
  v[kVpHeight] = static_cast<int32_t>(target.h);
  v[kVpScissorLeft] = v[kVpLeft];
  v[kVpScissorTop] = v[kVpTop];
  v[kVpScissorWidth] = v[kVpWidth];
  v[kVpScissorHeight] = v[kVpHeight];

  g_routed_total.fetch_add(1, std::memory_order_relaxed);
  NoteCensus(owner, true);

  if ((g_frame.load(std::memory_order_relaxed) & 0x3F) == 0) {
    PlacedMovie placed;
    placed.movie = movie;
    placed.controller = owner;
    placed.source = source;
    placed.target = target;
    placed.interactive = route.interactive;
    placed.page = route.destination == Destination::kPage;
    placed.frame = g_frame.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> guard(g_placed_lock);
    g_placed.push_back(placed);
    if (placed.page) g_pages.fetch_add(1, std::memory_order_relaxed);
  }
  return true;
}

void DXS_THISCALL BeginDisplayHook(void* self, void* /*edx*/, unsigned bg, void* viewport,
                                   float x0, float x1, float y0, float y1) {
  NoteCaller(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
  g_begins_this_frame.fetch_add(1, std::memory_order_relaxed);
  g_total_begins.fetch_add(1, std::memory_order_relaxed);

  // Save every pass, including an unchanged one, so nested Begin/End pairs pop
  // the correct record. A routed GViewport stays borrowed until EndDisplay:
  // DXHR builds its cdc render commands after BeginDisplay returns.
  int32_t original_viewport[kVpDwords];
  const bool saved_viewport =
      viewport && Writable(viewport, sizeof(original_viewport));
  if (saved_viewport) memcpy(original_viewport, viewport, sizeof(original_viewport));

  const bool changed_viewport = RouteCurrentPass(viewport);

  bool borrowed = false;
  if (g_viewport_borrow_count < kViewportBorrowDepth) {
    ViewportBorrow& borrow = g_viewport_borrows[g_viewport_borrow_count++];
    borrow.address = viewport;
    borrow.restore = changed_viewport && saved_viewport;
    if (saved_viewport) memcpy(borrow.value, original_viewport, sizeof(original_viewport));
    borrowed = true;
  } else {
    ++g_viewport_borrow_overflow;
    if (changed_viewport && saved_viewport) {
      DXS_WARN("[sf] nested display depth exceeded %d; this pass will not be relocated",
               kViewportBorrowDepth);
    }
  }

  // The viewport the pass was opened with, sampled. Its buffer size says which
  // surface Scaleform believes it is drawing into, which is the other half of
  // knowing where a movie ends up.
  const uint32_t n = g_total_begins.load(std::memory_order_relaxed);
  if ((n % 900) == 1 && viewport && Readable(viewport, 4 * 11)) {
    const int* v = static_cast<const int*>(viewport);
    DXS_INFO("[sf] BeginDisplay vp buffer=%dx%d rect=%d,%d %dx%d flags=0x%X"
             "  ortho x0=%.1f x1=%.1f y0=%.1f y1=%.1f",
             v[0], v[1], v[2], v[3], v[4], v[5], v[10], x0, x1, y0, y1);
  }
  if (o_BeginDisplay) o_BeginDisplay(self, nullptr, bg, viewport, x0, x1, y0, y1);
  if (!borrowed && changed_viewport && saved_viewport) {
    memcpy(viewport, original_viewport, sizeof(original_viewport));
  }
}

void ReportCallers() {
  const int n = g_caller_count.load();
  if (n <= 0) return;
  DXS_INFO("[sf] BeginDisplay callers (loaded address -> address in the 0x400000"
           " image the static analysis used):");
  for (int i = 0; i < n && i < kMaxCallers; ++i) {
    const uintptr_t site = g_callers[i].site;
    DXS_INFO("[sf]   %p -> 0x%08X   %u calls", reinterpret_cast<void*>(site),
             InModule(site) ? static_cast<unsigned>(Unbase(site)) : 0u,
             g_callers[i].count);
  }
}

// The choke point.
//
// Slot 4 is the last thing BeginDisplay does and the only place Scaleform's
// output is decided, so the binding put in place here is the one the draws that
// follow will use. Overriding after the original returns means this is the last
// writer, not a racer -- which is the difference between this and the approach
// that crashed.
// The pass is over: whatever the engine had bound goes back.
//
// A render target bound for a display pass has to be released at the end of
// that pass, not left for the next thing to draw. Leaving it is how the scene
// ends up on the bottom screen.
void DXS_THISCALL EndDisplayHook(void* self, void* /*edx*/) {
  g_total_ends.fetch_add(1, std::memory_order_relaxed);
  if (o_EndDisplay) o_EndDisplay(self, nullptr);
  if (g_viewport_borrow_overflow > 0) {
    --g_viewport_borrow_overflow;
  } else if (g_viewport_borrow_count > 0) {
    ViewportBorrow& borrow = g_viewport_borrows[--g_viewport_borrow_count];
    if (borrow.restore && borrow.address && Writable(borrow.address, sizeof(borrow.value))) {
      memcpy(borrow.address, borrow.value, sizeof(borrow.value));
    }
    borrow.address = nullptr;
    borrow.restore = false;
  } else {
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true)) DXS_WARN("[sf] EndDisplay arrived without a matching BeginDisplay");
  }
}

void DXS_THISCALL ApplyHook(void* self, void* /*edx*/) {
  g_total_applies.fetch_add(1, std::memory_order_relaxed);
  if (o_Apply) o_Apply(self, nullptr);
}

// ---------------------------------------------------------------- slot survey
//
// Which slot of ScaleformMovieInstance actually runs every frame.
//
// Slot 12 is the one that reads the placement rectangle, and the assumption was
// that it therefore runs per element per frame. If it turns out not to be
// called at all, the question becomes which slot is -- and that cannot be
// answered by hooking them with C++ thunks, because the thirty-four slots have
// different signatures and calling one through a thunk of the wrong shape
// unbalances the stack.
//
// So these are pure tail jumps written in assembly: increment a counter, jump
// to the original. No arguments are touched, no stack is adjusted, and every
// register arrives at the original exactly as it would have. Flags are clobbered
// by the increment, which is safe -- they are not live across a call boundary.
extern "C" {
unsigned dxs_slot_calls[34];
void* dxs_slot_orig[34];
}

#define DXS_SLOT_STUB(n)                                    \
  extern "C" void dxs_slot_stub_##n(void);                  \
  asm(".globl _dxs_slot_stub_" #n "\n"                      \
      "_dxs_slot_stub_" #n ":\n"                            \
      "  incl _dxs_slot_calls+" #n "*4\n"                   \
      "  jmp  *_dxs_slot_orig+" #n "*4\n");

DXS_SLOT_STUB(0)  DXS_SLOT_STUB(1)  DXS_SLOT_STUB(2)  DXS_SLOT_STUB(3)
DXS_SLOT_STUB(4)  DXS_SLOT_STUB(5)  DXS_SLOT_STUB(6)  DXS_SLOT_STUB(7)
DXS_SLOT_STUB(8)  DXS_SLOT_STUB(9)  DXS_SLOT_STUB(10) DXS_SLOT_STUB(11)
DXS_SLOT_STUB(12) DXS_SLOT_STUB(13) DXS_SLOT_STUB(14) DXS_SLOT_STUB(15)
DXS_SLOT_STUB(16) DXS_SLOT_STUB(17) DXS_SLOT_STUB(18) DXS_SLOT_STUB(19)
DXS_SLOT_STUB(20) DXS_SLOT_STUB(21) DXS_SLOT_STUB(22) DXS_SLOT_STUB(23)
DXS_SLOT_STUB(24) DXS_SLOT_STUB(25) DXS_SLOT_STUB(26) DXS_SLOT_STUB(27)
DXS_SLOT_STUB(28) DXS_SLOT_STUB(29) DXS_SLOT_STUB(30) DXS_SLOT_STUB(31)
DXS_SLOT_STUB(32) DXS_SLOT_STUB(33)

void* SlotStub(int n) {
  static void* const kStubs[34] = {
      (void*)&dxs_slot_stub_0,  (void*)&dxs_slot_stub_1,  (void*)&dxs_slot_stub_2,
      (void*)&dxs_slot_stub_3,  (void*)&dxs_slot_stub_4,  (void*)&dxs_slot_stub_5,
      (void*)&dxs_slot_stub_6,  (void*)&dxs_slot_stub_7,  (void*)&dxs_slot_stub_8,
      (void*)&dxs_slot_stub_9,  (void*)&dxs_slot_stub_10, (void*)&dxs_slot_stub_11,
      (void*)&dxs_slot_stub_12, (void*)&dxs_slot_stub_13, (void*)&dxs_slot_stub_14,
      (void*)&dxs_slot_stub_15, (void*)&dxs_slot_stub_16, (void*)&dxs_slot_stub_17,
      (void*)&dxs_slot_stub_18, (void*)&dxs_slot_stub_19, (void*)&dxs_slot_stub_20,
      (void*)&dxs_slot_stub_21, (void*)&dxs_slot_stub_22, (void*)&dxs_slot_stub_23,
      (void*)&dxs_slot_stub_24, (void*)&dxs_slot_stub_25, (void*)&dxs_slot_stub_26,
      (void*)&dxs_slot_stub_27, (void*)&dxs_slot_stub_28, (void*)&dxs_slot_stub_29,
      (void*)&dxs_slot_stub_30, (void*)&dxs_slot_stub_31, (void*)&dxs_slot_stub_32,
      (void*)&dxs_slot_stub_33,
  };
  return (n >= 0 && n < 34) ? kStubs[n] : nullptr;
}

bool g_slots_observed = false;

void ObserveSlots(RttiIndex* rtti, HookSet* hooks, const char* class_name) {
  const RttiClass* entry = rtti->Find(class_name);
  if (!entry) return;
  const uint32_t count = entry->method_count < 34 ? entry->method_count : 34;
  uint32_t installed = 0;
  for (uint32_t i = 0; i < count; ++i) {
    // Slot 12 already carries the real hook, which counts itself.
    if (i == g_shape.placement_slot || i == kMovieDisplaySlot) continue;
    void* original = nullptr;
    if (!Read(entry->vtable + i, &original)) continue;
    dxs_slot_orig[i] = original;
    if (hooks->Add(entry->vtable, i, SlotStub(static_cast<int>(i)))) ++installed;
  }
  g_slots_observed = true;
  DXS_INFO("[sf] slot survey on %s: %u of %u slots counted", class_name, installed, count);
}

void ReportSlots() {
  if (!g_slots_observed) return;
  DXS_INFO("[sf] ScaleformMovieInstance slot call counts:");
  for (int i = 0; i < 34; ++i) {
    if (dxs_slot_calls[i] == 0) continue;
    DXS_INFO("[sf]   slot %-2d called %u times", i, dxs_slot_calls[i]);
  }
  DXS_INFO("[sf]   slot 12 (placement, hooked separately) called %u times",
           g_total_movies.load());
}

// ---------------------------------------------------------------- detection
void DecideMode() {
  const uint32_t frames = g_frames_sampled.load();
  if (frames < 90 || g_mode != PassMode::kUnknown) return;

  const uint32_t movies = g_movies_sampled.load();
  const uint32_t begins = g_begins_sampled.load();
  if (movies == 0) {
    // Should be unreachable: sampling does not begin until a frame has placed
    // something. Kept because "decided nothing from nothing" is a worse failure
    // than saying so.
    DXS_WARN("[sf] sampled %u frames with no placements -- not deciding", frames);
    return;
  }
  // If the interface gets a display pass each, the two counts track. One pass
  // for the lot leaves begins at roughly one per frame while movies is dozens.
  const double ratio = static_cast<double>(begins) / static_cast<double>(movies);
  DXS_INFO("[sf] over %u frames: %u placements, %u BeginDisplay calls (ratio %.3f)",
           frames, movies, begins, ratio);

  if (ratio >= 0.6) {
    g_mode = PassMode::kPerMovie;
    DXS_INFO("[sf] BeginDisplay runs per element. Per-element routing is available"
             " and the panel is live.");
  } else {
    g_mode = PassMode::kPerFrame;
    g_enabled = false;
    DXS_WARN("[sf] BeginDisplay runs once for the whole interface (%.3f passes per"
             " element). There is one output surface for all of it, so routing"
             " individual elements to the panel is not possible at this seam.",
             ratio);
    DXS_WARN("[sf] Standing down: every element stays on the main screen. The game is"
             " unaffected. Resolving this needs the per-element draw site, which is"
             " the one thing static analysis could not settle.");
  }
}

}  // namespace

ScaleformBridge& TheBridge() { return g_bridge; }

void ScaleformBridge::BeginFrame(uint32_t frame) {
  g_frame.store(frame, std::memory_order_relaxed);

  // Published once per primary game frame and read by the tiny assembly stub
  // above. An aligned 32-bit store is atomic on every supported x86 target.
  // Native modal menus deliberately restore the retail root; the custom lower
  // pages never take the game's menu-input lease, so they remain independent.
  dxs_suppress_display =
      (g_native_interception && g_enabled && g_route_all && ThePanel().ready() &&
       ThePanel().game_loaded() && !ThePanel().menu_input_locked()) ? 1u : 0u;

  g_movies_this_frame.exchange(0, std::memory_order_relaxed);
  const uint32_t begins = g_begins_this_frame.exchange(0, std::memory_order_relaxed);

  // Displays come from the assembly stub's counter, which is the only thing
  // that sees slot 26. Sampled as a delta so a paused frame does not skew it.
  static unsigned last_displays = 0;
  const unsigned now_displays = dxs_display_count;
  g_displays_this_frame.store(now_displays - last_displays, std::memory_order_relaxed);
  last_displays = now_displays;

  // Sampling starts when the interface starts, not at a fixed frame number.
  //
  // The first attempt sampled frames 30 to 120, which on this game is entirely
  // inside the publisher logos and the load -- nothing is drawn there, so it
  // measured ninety empty frames and concluded nothing, permanently. Waiting
  // for the first placement before counting is the difference between measuring
  // the interface and measuring the loading screen.
  const uint32_t displays = g_displays_this_frame.exchange(0, std::memory_order_relaxed);
  if (g_mode == PassMode::kUnknown && displays > 0 && g_frames_sampled.load() < 90) {
    g_frames_sampled.fetch_add(1, std::memory_order_relaxed);
    g_movies_sampled.fetch_add(displays, std::memory_order_relaxed);
    g_begins_sampled.fetch_add(begins, std::memory_order_relaxed);
    DecideMode();
  }

  // Cumulative reach, reported whether or not anything was decided. When the
  // panel is empty the first question is not "what is the ratio" but "is the
  // seam reached at all", and only these numbers answer it.
  if (frame > 0 && (frame % 600) == 0) {
    DXS_INFO("[sf] reach at frame %u: placements=%u BeginDisplay=%u apply=%u",
             frame, g_total_movies.load(), g_total_begins.load(), g_total_applies.load());
    DXS_INFO("[sf] BeginDisplay=%u EndDisplay=%u (a gap here means a pass kept the"
             " panel's target past its own end)", g_total_begins.load(),
             g_total_ends.load());
    DXS_INFO("[sf] displays (slot 26) = %u", dxs_display_count);
    DXS_INFO("[sf] gameplay root displays suppressed = %u",
             dxs_suppressed_display_count);
    ReportCensus();
    ReportInstances();
    if (!g_route_all && g_total_movies.load() == 0 &&
        g_total_begins.load() > 0) {
      DXS_WARN("[sf] the renderer is running but ScaleformMovieInstance slot 12 has"
               " never been called. It computes a placement transform, so the engine"
               " may only call it when a movie is created or resized rather than"
               " every frame -- in which case it is the wrong seam for per-frame"
               " routing and the draw itself has to be found instead.");
    }
    if (g_total_begins.load() == 0) {
      DXS_WARN("[sf] BeginDisplay has never been called either, so the interface is"
               " not going through the renderer this mod hooked.");
    }
    ReportSlots();
    ReportCallers();
  }

  std::lock_guard<std::mutex> guard(g_placed_lock);
  g_placed_last.swap(g_placed);
  g_placed.clear();
  page_count_ = g_pages.exchange(0, std::memory_order_relaxed);
  mode_ = g_mode;
}

std::vector<PlacedMovie> ScaleformBridge::Placed() const {
  std::lock_guard<std::mutex> guard(g_placed_lock);
  return g_placed_last;
}

bool ScaleformBridge::Attach(RttiIndex* rtti, HookSet* hooks, ID3D11DeviceContext* context) {
  if (!rtti || !hooks || !context) return false;
  const size_t hook_checkpoint = hooks->count();
  g_rtti = rtti;

  g_shape.placement_slot = static_cast<uint32_t>(
      Cfg().GetInt("advanced.placementslot", static_cast<int>(g_shape.placement_slot)));
  g_shape.descriptor = static_cast<uint32_t>(
      Cfg().GetInt("advanced.descriptor", static_cast<int>(g_shape.descriptor)));
  // The custom panel is independent of Scaleform. Never install a native HUD
  // hook merely because a legacy INI disables whole_ui or relocate: stale test
  // settings must fail safe. The single explicit experimental switch is the
  // only route into this undocumented vtable interception path.
  g_native_interception =
      Cfg().GetBool("panel.suppress_native_hud_experimental", false);
  if (!g_native_interception) {
    g_enabled = false;
    g_route_all = true;
    g_mode = PassMode::kWholeUi;
    attached_ = true;
    mode_ = PassMode::kWholeUi;
    dxs_suppress_display = 0;
    DXS_INFO("[sf] custom panel isolation: native Scaleform interception disabled;"
             " the retail upper HUD remains untouched");
    return true;
  }

  // Runtime evidence shows DXHR owns the visible HUD as child movies inside a
  // single anonymous root. Experimental suppression therefore has one coherent
  // mode: suppress that entire root. Per-element relocation is not safe or
  // supported and cannot be re-enabled by old panel.whole_ui/test.route_all
  // values.
  g_enabled = true;
  g_route_all = true;
  g_mode = PassMode::kWholeUi;
  DXS_WARN("[sf] experimental whole-HUD suppression enabled; native Scaleform"
           " vtables will be intercepted");

  struct Item { const char* cls; uint32_t slot; void* fn; void** cache; const char* what; };
  const Item items[] = {
      {"ScaleformMovieInstance", g_shape.placement_slot,
       reinterpret_cast<void*>(&PlacementHook), reinterpret_cast<void**>(&o_Placement),
       "placement"},
      {"ScaleformRenderer", kBeginDisplaySlot, reinterpret_cast<void*>(&BeginDisplayHook),
       reinterpret_cast<void**>(&o_BeginDisplay), "BeginDisplay"},
      {"ScaleformRenderer", kApplySlot, reinterpret_cast<void*>(&ApplyHook),
       reinterpret_cast<void**>(&o_Apply), "apply output"},
      {"ScaleformRenderer", kEndDisplaySlot, reinterpret_cast<void*>(&EndDisplayHook),
       reinterpret_cast<void**>(&o_EndDisplay), "end display"},
  };

  bool item_hooked[sizeof(items) / sizeof(items[0])] = {};
  for (size_t item_index = 0; item_index < sizeof(items) / sizeof(items[0]); ++item_index) {
    const Item& it = items[item_index];
    // The custom whole-interface path never relocates individual native
    // elements. Leaving placement completely vanilla avoids permanent HUD
    // descriptor edits that could otherwise leak into a later modal menu.
    if (g_route_all && item_index == 0) {
      DXS_INFO("[sf] individual placement hook omitted in whole-interface mode");
      continue;
    }
    const RttiClass* entry = rtti->Find(it.cls);
    if (!entry) { DXS_ERR("[sf] %s is not in the RTTI index", it.cls); continue; }
    if (it.slot >= entry->method_count) {
      DXS_ERR("[sf] %s has %u slots; cannot hook %u", it.cls, entry->method_count, it.slot);
      continue;
    }
    Read(entry->vtable + it.slot, it.cache);
    if (hooks->Add(entry->vtable, it.slot, it.fn)) {
      DXS_INFO("[sf] hooked %s slot %u (%s), original %p", it.cls, it.slot, it.what, *it.cache);
      item_hooked[item_index] = true;
    } else {
      DXS_ERR("[sf] could not hook %s slot %u", it.cls, it.slot);
      *it.cache = nullptr;
    }
  }

  // ScaleformChildMovieInstance implements the same interface but is a separate
  // class with an unrelated vtable whose slot 12 is not a placement method.
  // Hooking it would patch an arbitrary function; it is deliberately left alone.
  if (const RttiClass* child = rtti->Find("ScaleformChildMovieInstance")) {
    DXS_INFO("[sf] ScaleformChildMovieInstance left unhooked by design (%u slots)",
             child->method_count);
  }

  // The display seam: slot 26, hooked with the tail-jump stub so its unknown
  // argument count cannot unbalance the stack.
  bool display_hooked = false;
  if (const RttiClass* mi = rtti->Find("ScaleformMovieInstance")) {
    if (kMovieDisplaySlot < mi->method_count) {
      Read(mi->vtable + kMovieDisplaySlot, &dxs_display_orig);
      if (hooks->Add(mi->vtable, kMovieDisplaySlot,
                      reinterpret_cast<void*>(&dxs_display_stub))) {
        DXS_INFO("[sf] hooked ScaleformMovieInstance slot %u (display), original %p",
                 kMovieDisplaySlot, dxs_display_orig);
        display_hooked = true;
      } else {
        DXS_ERR("[sf] could not hook the display slot; nothing can be routed");
        dxs_display_orig = nullptr;
      }
    }
  }

  if (Cfg().GetBool("advanced.observeslots", false)) {
    ObserveSlots(rtti, hooks, "ScaleformMovieInstance");
  }

  // Placement is diagnostic/optional on this build: gameplay proved slot 12
  // is not called in steady state. The display stub supplies movie identity;
  // BeginDisplay applies the temporary empty scissor, and EndDisplay restores
  // it. No engine-command or D3D-callsite detours are required.
  attached_ = item_hooked[1] && item_hooked[2] && item_hooked[3] &&
              display_hooked;
  if (!attached_) {
    DXS_ERR("[sf] required seams missing (begin=%d apply=%d end=%d display=%d);"
            " relocation stays off", item_hooked[1], item_hooked[2],
            item_hooked[3], display_hooked);
    g_enabled = false;
    hooks->Rollback(hook_checkpoint);
  }
  return attached_;
}

void ScaleformBridge::Detach() {
  dxs_suppress_display = 0;
  g_native_interception = false;
  g_enabled = false;
  attached_ = false;
}

void ScaleformBridge::LogReport() const {
  DXS_INFO("[sf] mode=%s pages=%d",
            mode_ == PassMode::kWholeUi ? "independent custom panel"
           : mode_ == PassMode::kPerMovie ? "per-element"
           : mode_ == PassMode::kPerFrame ? "per-frame (stood down)" : "undecided",
           page_count_);
  if (g_native_interception) {
    DXS_INFO("[sf] legacy gameplay UI hidden at root Display (%u calls suppressed);"
             " native modal screens remain on the upper display",
             dxs_suppressed_display_count);
  } else {
    DXS_INFO("[sf] native Scaleform interception is disabled; upper HUD is vanilla");
  }
  std::lock_guard<std::mutex> guard(g_placed_lock);
  for (const PlacedMovie& p : g_placed_last) {
    DXS_INFO("[sf]   %-44s %.0f,%.0f %.0fx%.0f -> %.0f,%.0f %.0fx%.0f%s",
             p.controller.c_str(), p.source.x, p.source.y, p.source.w, p.source.h,
             p.target.x, p.target.y, p.target.w, p.target.h,
             p.page ? "  [page]" : "");
  }
}

}  // namespace dxs

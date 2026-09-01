// Intercepting the interface and sending the chosen parts to the panel.
//
// Three facts from the executable hold this up, and the middle one is the one
// the previous attempt had wrong:
//
//  1. Every element is a separate ScaleformMovieInstance owned by a named
//     Ns*MovieController, and RTTI names all eighty-eight of them.
//
//  2. ScaleformMovieInstance slot 12 is *placement*, not drawing. It reads the
//     rectangle at descriptor +0x30/+0x34/+0x38/+0x3C, calls 0x0072E4C0, and
//     that stores a transform at instance+0x190. Rewriting the rectangle before
//     the original runs relocates the element, because the transform is
//     computed from it. Wrapping a render-target swap around it does nothing,
//     because nothing is drawn inside it -- which is why three attempts at that
//     produced a blank panel and then a crash as the capture window was widened
//     looking for the draws.
//
//  3. Scaleform binds its output through the engine, not through D3D11
//     directly: ScaleformRenderer slot 12 (BeginDisplay) ends by calling its own
//     slot 4, which routes through the object at 0x00EAAB48 passing
//     renderer+0x260/+0x264 as width and height. Slot 4 is therefore the single
//     choke point where output is decided, and it runs *before* the draws it
//     applies to. Overriding the binding immediately after the original slot 4
//     returns makes this code the last writer rather than a racer.
//
// What is not yet known, and cannot be settled without running the game, is
// whether BeginDisplay runs once per movie or once per frame. Everything below
// works if it is per movie. If it is per frame there is one surface for the
// whole interface and no per-element routing is possible at this seam, so the
// mod detects that case and stands down rather than putting the whole HUD on
// the wrong screen. Both outcomes are written to the log on the first run.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Layout.h"

struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

namespace dxs {

class RttiIndex;
class HookSet;

// How BeginDisplay turned out to behave, decided from the first hundred frames.
enum class PassMode {
  kUnknown,
  kWholeUi,    // supported path: the custom panel replaces the native UI root
  kPerMovie,   // one display pass per element: full routing is possible
  kPerFrame,   // one pass for the whole interface: routing at this seam is not
};

// An element that was placed on the panel this frame, kept so a touch can be
// mapped back onto it.
struct PlacedMovie {
  void* movie = nullptr;
  std::string controller;
  Rect source;   // where the game believes it is, in game-surface pixels
  Rect target;   // where it was put, in panel pixels
  bool interactive = false;
  bool page = false;
  uint32_t frame = 0;
};

class ScaleformBridge {
 public:
  bool Attach(RttiIndex* rtti, HookSet* hooks, ID3D11DeviceContext* context);
  void Detach();

  bool attached() const { return attached_; }
  PassMode mode() const { return mode_; }

  // Elements on the panel in the most recent frame, back to front, which is
  // also the order to hit-test them in reverse.
  std::vector<PlacedMovie> Placed() const;

  // How many pages are open. The zones behind a page are dimmed, and only the
  // topmost page receives touches.
  int page_count() const { return page_count_; }

  void BeginFrame(uint32_t frame);
  void LogReport() const;

 private:
  bool attached_ = false;
  PassMode mode_ = PassMode::kUnknown;
  int page_count_ = 0;
};

ScaleformBridge& TheBridge();

}  // namespace dxs

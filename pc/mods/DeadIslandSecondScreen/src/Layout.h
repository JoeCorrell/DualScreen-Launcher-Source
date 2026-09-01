// What goes where on the 1240x1080 panel.
//
// The panel is proportionally much taller than the 16:9 the interface was drawn
// for -- 1.148:1 against 1.778:1 -- and that spare height is the room this
// layout is built out of. Nothing is squeezed: each element is given a zone
// with its own aspect and scaled to fit inside it, so the radar stays round and
// the bars stay the thickness they were drawn at.
//
//   y=0     +------------------------------------------------+
//           |                    VITALS                      |  health, energy, sprint
//   y=136   +--------------------------+---------------------+
//           |                          |                     |
//           |          SENSOR          |     OBJECTIVES      |
//           |                          |                     |
//   y=616   +--------------------------+                     |
//           |          WEAPON          |                     |
//   y=916   +--------------------------+---------------------+
//           |             CONTEXT / QUICKBAR                 |
//   y=1080  +------------------------------------------------+
//
// A page -- inventory, the augmentation tree, the map, hacking -- takes the
// whole panel and the persistent zones are dimmed behind it, because those are
// screens the player stops to read rather than glances at.

#pragma once

#include <string>
#include <unordered_map>

namespace dis {

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
  float right() const { return x + w; }
  float bottom() const { return y + h; }
  bool Contains(float px, float py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};

// Where an element ends up.
enum class Destination {
  kMain,    // left exactly where the game put it
  kPanel,   // relocated into a named zone
  kPage,    // relocated to the whole panel, on top of the zones
  kHidden,  // not drawn at all
};

struct Route {
  Destination destination = Destination::kMain;
  std::string zone;      // when kPanel
  bool interactive = false;  // whether a touch on it should be forwarded
};

class Layout {
 public:
  // Builds the zones for a panel of this size and reads any [zones] and [route]
  // overrides out of the ini.
  void Build(int panel_width, int panel_height);

  int width() const { return width_; }
  int height() const { return height_; }

  const Rect* Zone(const std::string& name) const;
  // Full-page movies sit below the persistent native tab bar.
  Rect Page() const;
  int HeaderHeight() const { return header_height_; }

  // The routing decision for a controller class. Unknown classes answer kMain,
  // which is the safe direction: an element nobody has classified stays where
  // it has always been rather than vanishing.
  Route For(const std::string& controller) const;

  // Fits `source` inside `zone` preserving aspect, centred. This is what keeps
  // a 16:9 element from being stretched into a 1.15:1 hole.
  static Rect FitInside(const Rect& zone, float source_w, float source_h);

  void LogSummary() const;

 private:
  int width_ = 1240;
  int height_ = 1080;
  int header_height_ = 0;
  std::unordered_map<std::string, Rect> zones_;
  std::unordered_map<std::string, Route> routes_;
};

Layout& TheLayout();

}  // namespace dis

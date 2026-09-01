// The drawing surface for the bottom screen.
//
// This is a CPU rasteriser, and that is a deliberate choice rather than a
// shortcut. The panel already uploads a finished RGBA buffer with
// UpdateSubresource, so there is a working path that needs no shaders -- and
// needing no shaders is worth a great deal here, because the toolchain is
// mingw-w64 and pulling in d3dcompiler to build HLSL at runtime would add a
// DLL dependency that has to exist inside the Wine prefix. Nothing about a
// 2D interface needs the GPU: at 1240x1080 a full repaint is 1.34M pixels,
// which is a millisecond of straight-line stores.
//
// The buffer is a GDI DIB section, which is the other half of the choice. It
// means text is drawn by the system's own rasteriser at any size, with real
// kerning and antialiasing, instead of by scaling up a baked bitmap font until
// it looks like a spreadsheet. GDI works under Wine and the proxy already
// imports GDI32.
//
// Pixels are stored BGRA because that is what a DIB is and what GDI expects.
// The swapchain wants RGBA -- the game's back buffer is format 29 -- so the
// red and blue channels are exchanged once, in CopyTo, on the way out. Doing
// it there rather than at every draw call keeps the swap in one place.

#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>

namespace dis {

struct Color {
  uint8_t r = 0, g = 0, b = 0, a = 255;
  constexpr Color() = default;
  constexpr Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
      : r(red), g(green), b(blue), a(alpha) {}

  // Same colour at a different opacity. The interface leans on this heavily --
  // most of its greys are one accent at four alphas rather than four greys.
  constexpr Color Alpha(uint8_t alpha) const { return Color(r, g, b, alpha); }
  Color Mix(const Color& other, float t) const;
};

// Named Box rather than Rect because dis::Rect already exists in Layout.h and
// means something else: that one is float-valued and describes where a *game*
// UI element sits in the engine's coordinates. This one is integer pixels on
// the panel. Merging them would force one of the two to carry the other's
// rounding behaviour, so they stay separate.
struct Box {
  int x = 0, y = 0, w = 0, h = 0;
  constexpr Box() = default;
  constexpr Box(int rx, int ry, int rw, int rh) : x(rx), y(ry), w(rw), h(rh) {}

  int right() const { return x + w; }
  int bottom() const { return y + h; }
  bool Contains(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
  Box Inset(int by) const { return Box(x + by, y + by, w - 2 * by, h - 2 * by); }
  Box Inset(int dx, int dy) const { return Box(x + dx, y + dy, w - 2 * dx, h - 2 * dy); }
};

enum class Align { kLeft, kCenter, kRight };

// A font request. Resolved and cached by the canvas, so a page asks for
// "22px semibold" every frame without paying for CreateFont every frame.
struct FontSpec {
  int size = 22;
  int weight = FW_NORMAL;
  bool italic = false;

  bool operator<(const FontSpec& other) const {
    if (size != other.size) return size < other.size;
    if (weight != other.weight) return weight < other.weight;
    return italic < other.italic;
  }
};

class Canvas {
 public:
  ~Canvas();

  bool Create(int width, int height);
  void Destroy();
  bool ready() const { return bits_ != nullptr; }

  int width() const { return width_; }
  int height() const { return height_; }
  HDC dc() const { return dc_; }

  // ---- clipping -------------------------------------------------------
  // Every draw is clipped to this. Pages set it around a scrolling list so a
  // row that runs past the bottom is cut rather than drawn over the tab bar.
  void SetClip(const Box& r);
  void ClearClip();

  // ---- fills ----------------------------------------------------------
  void Clear(const Color& c);
  void FillRect(const Box& r, const Color& c);
  void FillRoundRect(const Box& r, int radius, const Color& c);
  void FillCircle(int cx, int cy, int radius, const Color& c);
  void StrokeRect(const Box& r, int thickness, const Color& c);
  void StrokeRoundRect(const Box& r, int radius, int thickness, const Color& c);
  void StrokeCircle(int cx, int cy, int radius, int thickness, const Color& c);
  void VGradient(const Box& r, const Color& top, const Color& bottom);
  void HGradient(const Box& r, const Color& left, const Color& right);
  void Line(int x0, int y0, int x1, int y1, int thickness, const Color& c);

  // A filled meter. `ratio` below zero means the value could not be read, and
  // that draws the track with a hatch rather than an empty bar -- the whole
  // point of carrying -1 this far is that it must not look like zero.
  void Bar(const Box& r, float ratio, const Color& fill, const Color& track);

  // Nearest-neighbour blit from an owned RGBA atlas. UI art is authored as
  // crisp iconography, so nearest sampling preserves its hard edges while the
  // tint lets locked/available skill states reuse the real game icon.
  void BlitRgba(const Box& destination, const uint8_t* rgba,
                int source_width, int source_height, const Box& source,
                const Color& tint = Color(255, 255, 255, 255));

  // ---- text -----------------------------------------------------------
  void Text(const Box& r, const std::string& text, const FontSpec& font,
            const Color& c, Align align = Align::kLeft, bool vcenter = true);
  void TextWrap(const Box& r, const std::string& text, const FontSpec& font,
                const Color& c, Align align = Align::kLeft);
  int TextWidth(const std::string& text, const FontSpec& font);
  int TextHeight(const FontSpec& font);

  // ---- output ---------------------------------------------------------
  // Writes the canvas out as RGBA, swapping red and blue on the way. `stride`
  // is the destination row pitch in bytes.
  void CopyTo(uint8_t* rgba, size_t stride) const;

 private:
  uint32_t* Row(int y) const { return bits_ + static_cast<size_t>(y) * width_; }
  void Blend(int x, int y, const Color& c);
  void BlendSpan(int x0, int x1, int y, const Color& c);
  HFONT FontFor(const FontSpec& spec);

  HDC dc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ previous_ = nullptr;
  uint32_t* bits_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  Box clip_;
  std::map<FontSpec, HFONT> fonts_;
};

}  // namespace dis

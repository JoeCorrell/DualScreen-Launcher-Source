#include "Canvas.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dis {
namespace {

inline uint8_t Lerp8(uint8_t a, uint8_t b, float t) {
  const float v = a + (b - a) * t;
  if (v <= 0.0f) return 0;
  if (v >= 255.0f) return 255;
  return static_cast<uint8_t>(v + 0.5f);
}

// Packs into the DIB's native order. A 32-bit DIB row is B,G,R,A in memory,
// which little-endian makes 0xAARRGGBB as a DWORD.
inline uint32_t Pack(const Color& c) {
  return (static_cast<uint32_t>(c.a) << 24) | (static_cast<uint32_t>(c.r) << 16) |
         (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

std::wstring ToWide(const std::string& text) {
  if (text.empty()) return {};
  UINT code_page = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  int count = MultiByteToWideChar(code_page, flags, text.data(),
                                  static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) {
    // Older localized packs can use the active Windows code page rather than
    // UTF-8. Preserve them instead of rejecting every high byte.
    code_page = CP_ACP;
    flags = 0;
    count = MultiByteToWideChar(code_page, flags, text.data(),
                                static_cast<int>(text.size()), nullptr, 0);
  }
  if (count <= 0) return {};
  std::wstring wide(static_cast<std::size_t>(count), L'\0');
  if (MultiByteToWideChar(code_page, flags, text.data(),
                          static_cast<int>(text.size()), wide.data(), count) !=
      count) {
    return {};
  }
  return wide;
}

}  // namespace

Color Color::Mix(const Color& other, float t) const {
  if (t <= 0.0f) return *this;
  if (t >= 1.0f) return other;
  return Color(Lerp8(r, other.r, t), Lerp8(g, other.g, t), Lerp8(b, other.b, t),
               Lerp8(a, other.a, t));
}

Canvas::~Canvas() { Destroy(); }

bool Canvas::Create(int width, int height) {
  Destroy();
  if (width <= 0 || height <= 0) return false;

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  // Negative height makes the DIB top-down, so row 0 is the top of the screen
  // and the buffer can be handed to the swapchain without being flipped.
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  HDC screen = GetDC(nullptr);
  dc_ = CreateCompatibleDC(screen);
  ReleaseDC(nullptr, screen);
  if (!dc_) return false;

  void* bits = nullptr;
  bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bitmap_ || !bits) {
    Destroy();
    return false;
  }
  previous_ = SelectObject(dc_, bitmap_);
  bits_ = static_cast<uint32_t*>(bits);
  width_ = width;
  height_ = height;
  clip_ = Box(0, 0, width, height);
  SetBkMode(dc_, TRANSPARENT);
  return true;
}

void Canvas::Destroy() {
  for (auto& entry : fonts_) {
    if (entry.second) DeleteObject(entry.second);
  }
  fonts_.clear();
  if (dc_ && previous_) SelectObject(dc_, previous_);
  if (bitmap_) DeleteObject(bitmap_);
  if (dc_) DeleteDC(dc_);
  dc_ = nullptr;
  bitmap_ = nullptr;
  previous_ = nullptr;
  bits_ = nullptr;
  width_ = height_ = 0;
}

// ------------------------------------------------------------------ clipping
void Canvas::SetClip(const Box& r) {
  const int x0 = std::max(0, r.x);
  const int y0 = std::max(0, r.y);
  const int x1 = std::min(width_, r.right());
  const int y1 = std::min(height_, r.bottom());
  clip_ = Box(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
  if (dc_) {
    HRGN region = CreateRectRgn(clip_.x, clip_.y, clip_.right(), clip_.bottom());
    SelectClipRgn(dc_, region);
    DeleteObject(region);
  }
}

void Canvas::ClearClip() {
  clip_ = Box(0, 0, width_, height_);
  if (dc_) SelectClipRgn(dc_, nullptr);
}

// --------------------------------------------------------------------- fills
void Canvas::Blend(int x, int y, const Color& c) {
  if (c.a == 0) return;
  if (x < clip_.x || y < clip_.y || x >= clip_.right() || y >= clip_.bottom()) return;
  uint32_t* pixel = Row(y) + x;
  if (c.a == 255) {
    *pixel = Pack(c);
    return;
  }
  const uint32_t dst = *pixel;
  const float sa = c.a / 255.0f;
  const uint8_t dr = static_cast<uint8_t>((dst >> 16) & 0xff);
  const uint8_t dg = static_cast<uint8_t>((dst >> 8) & 0xff);
  const uint8_t db = static_cast<uint8_t>(dst & 0xff);
  *pixel = Pack(Color(Lerp8(dr, c.r, sa), Lerp8(dg, c.g, sa), Lerp8(db, c.b, sa), 255));
}

void Canvas::BlendSpan(int x0, int x1, int y, const Color& c) {
  if (c.a == 0) return;
  if (y < clip_.y || y >= clip_.bottom()) return;
  x0 = std::max(x0, clip_.x);
  x1 = std::min(x1, clip_.right());
  if (x1 <= x0) return;

  uint32_t* row = Row(y);
  if (c.a == 255) {
    const uint32_t packed = Pack(c);
    for (int x = x0; x < x1; ++x) row[x] = packed;
    return;
  }
  const float sa = c.a / 255.0f;
  for (int x = x0; x < x1; ++x) {
    const uint32_t dst = row[x];
    const uint8_t dr = static_cast<uint8_t>((dst >> 16) & 0xff);
    const uint8_t dg = static_cast<uint8_t>((dst >> 8) & 0xff);
    const uint8_t db = static_cast<uint8_t>(dst & 0xff);
    row[x] = Pack(Color(Lerp8(dr, c.r, sa), Lerp8(dg, c.g, sa), Lerp8(db, c.b, sa), 255));
  }
}

void Canvas::Clear(const Color& c) {
  if (!bits_) return;
  const uint32_t packed = Pack(c);
  std::fill_n(bits_, static_cast<size_t>(width_) * height_, packed);
}

void Canvas::FillRect(const Box& r, const Color& c) {
  if (!bits_ || r.w <= 0 || r.h <= 0) return;
  const int y1 = std::min(r.bottom(), clip_.bottom());
  for (int y = std::max(r.y, clip_.y); y < y1; ++y) BlendSpan(r.x, r.right(), y, c);
}

void Canvas::FillRoundRect(const Box& r, int radius, const Color& c) {
  if (!bits_ || r.w <= 0 || r.h <= 0) return;
  radius = std::min(radius, std::min(r.w, r.h) / 2);
  if (radius <= 0) { FillRect(r, c); return; }

  const int y1 = std::min(r.bottom(), clip_.bottom());
  for (int y = std::max(r.y, clip_.y); y < y1; ++y) {
    int inset = 0;
    const int dy_top = r.y + radius - y;
    const int dy_bottom = y - (r.bottom() - radius - 1);
    const int dy = std::max(dy_top, dy_bottom);
    if (dy > 0) {
      // Circle edge for this row. The half-pixel keeps the corner from
      // reading a step short against a straight neighbour.
      const float dx = std::sqrt(std::max(0.0f, static_cast<float>(radius * radius) -
                                                static_cast<float>(dy * dy)));
      inset = radius - static_cast<int>(dx + 0.5f);
    }
    BlendSpan(r.x + inset, r.right() - inset, y, c);
  }
}

void Canvas::FillCircle(int cx, int cy, int radius, const Color& c) {
  if (!bits_ || radius <= 0) return;
  for (int y = -radius; y <= radius; ++y) {
    const int half = static_cast<int>(std::sqrt(std::max(
        0, radius * radius - y * y)));
    BlendSpan(cx - half, cx + half + 1, cy + y, c);
  }
}

void Canvas::StrokeRect(const Box& r, int thickness, const Color& c) {
  if (thickness <= 0 || r.w <= 0 || r.h <= 0) return;
  FillRect(Box(r.x, r.y, r.w, thickness), c);
  FillRect(Box(r.x, r.bottom() - thickness, r.w, thickness), c);
  FillRect(Box(r.x, r.y + thickness, thickness, r.h - 2 * thickness), c);
  FillRect(Box(r.right() - thickness, r.y + thickness, thickness, r.h - 2 * thickness), c);
}

void Canvas::StrokeRoundRect(const Box& r, int radius, int thickness, const Color& c) {
  if (!bits_ || thickness <= 0 || r.w <= 0 || r.h <= 0) return;
  radius = std::min(radius, std::min(r.w, r.h) / 2);
  if (radius <= 0) { StrokeRect(r, thickness, c); return; }

  // The outline is the region between two rounded rectangles, filled row by
  // row. Drawing the outer shape and then trying to punch the middle back out
  // would need the colour that was underneath, which is gone by then -- so the
  // difference is computed per row instead and only the border spans are
  // touched. That also leaves whatever the caller already drew inside intact.
  const Box inner = r.Inset(thickness);
  const int inner_radius = std::max(0, radius - thickness);

  auto inset_for = [](int y, const Box& box, int rad) -> int {
    if (rad <= 0) return 0;
    const int dy = std::max(box.y + rad - y, y - (box.bottom() - rad - 1));
    if (dy <= 0) return 0;
    const float dx = std::sqrt(std::max(0.0f, static_cast<float>(rad * rad) -
                                              static_cast<float>(dy * dy)));
    return rad - static_cast<int>(dx + 0.5f);
  };

  const int y1 = std::min(r.bottom(), clip_.bottom());
  for (int y = std::max(r.y, clip_.y); y < y1; ++y) {
    const int outer_inset = inset_for(y, r, radius);
    const int left = r.x + outer_inset;
    const int right = r.right() - outer_inset;

    if (y < inner.y || y >= inner.bottom() || inner.w <= 0 || inner.h <= 0) {
      BlendSpan(left, right, y, c);
      continue;
    }
    const int inner_inset = inset_for(y, inner, inner_radius);
    BlendSpan(left, inner.x + inner_inset, y, c);
    BlendSpan(inner.right() - inner_inset, right, y, c);
  }
}

void Canvas::StrokeCircle(int cx, int cy, int radius, int thickness,
                          const Color& c) {
  if (!bits_ || radius <= 0 || thickness <= 0) return;
  const int inner = std::max(0, radius - thickness);
  for (int y = -radius; y <= radius; ++y) {
    const int outer_half = static_cast<int>(std::sqrt(std::max(
        0, radius * radius - y * y)));
    if (inner == 0 || std::abs(y) >= inner) {
      BlendSpan(cx - outer_half, cx + outer_half + 1, cy + y, c);
      continue;
    }
    const int inner_half = static_cast<int>(std::sqrt(std::max(
        0, inner * inner - y * y)));
    BlendSpan(cx - outer_half, cx - inner_half, cy + y, c);
    BlendSpan(cx + inner_half + 1, cx + outer_half + 1, cy + y, c);
  }
}

void Canvas::VGradient(const Box& r, const Color& top, const Color& bottom) {
  if (!bits_ || r.h <= 0) return;
  const int y1 = std::min(r.bottom(), clip_.bottom());
  for (int y = std::max(r.y, clip_.y); y < y1; ++y) {
    const float t = r.h > 1 ? static_cast<float>(y - r.y) / (r.h - 1) : 0.0f;
    BlendSpan(r.x, r.right(), y, top.Mix(bottom, t));
  }
}

void Canvas::HGradient(const Box& r, const Color& left, const Color& right) {
  if (!bits_ || r.w <= 0) return;
  const int y1 = std::min(r.bottom(), clip_.bottom());
  for (int y = std::max(r.y, clip_.y); y < y1; ++y) {
    for (int x = r.x; x < r.right(); ++x) {
      const float t = r.w > 1 ? static_cast<float>(x - r.x) / (r.w - 1) : 0.0f;
      Blend(x, y, left.Mix(right, t));
    }
  }
}

void Canvas::Line(int x0, int y0, int x1, int y1, int thickness, const Color& c) {
  const int dx = std::abs(x1 - x0);
  const int dy = std::abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  const int half = std::max(0, thickness / 2);
  for (;;) {
    for (int oy = -half; oy <= half; ++oy) {
      for (int ox = -half; ox <= half; ++ox) Blend(x0 + ox, y0 + oy, c);
    }
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 < dx) { err += dx; y0 += sy; }
  }
}

void Canvas::Bar(const Box& r, float ratio, const Color& fill, const Color& track) {
  FillRoundRect(r, r.h / 2, track);
  if (ratio < 0.0f) {
    // Unavailable, which is not the same as empty. A diagonal hatch reads as
    // "no signal" at a glance and can never be mistaken for a value.
    for (int x = r.x; x < r.right(); x += 10) {
      Line(x, r.bottom() - 1, x + r.h, r.y, 1, track.Mix(Color(255, 255, 255), 0.25f));
    }
    return;
  }
  const int w = static_cast<int>(r.w * std::min(1.0f, std::max(0.0f, ratio)));
  if (w > 0) FillRoundRect(Box(r.x, r.y, w, r.h), r.h / 2, fill);
}

void Canvas::BlitRgba(const Box& destination, const uint8_t* rgba,
                      int source_width, int source_height, const Box& source,
                      const Color& tint) {
  if (!bits_ || !rgba || destination.w <= 0 || destination.h <= 0 ||
      source_width <= 0 || source_height <= 0 || source.w <= 0 ||
      source.h <= 0 || source.x < 0 || source.y < 0 ||
      source.right() > source_width || source.bottom() > source_height) {
    return;
  }
  const int y0 = std::max(destination.y, clip_.y);
  const int y1 = std::min(destination.bottom(), clip_.bottom());
  const int x0 = std::max(destination.x, clip_.x);
  const int x1 = std::min(destination.right(), clip_.right());
  for (int y = y0; y < y1; ++y) {
    const int sy = source.y +
        static_cast<int>((static_cast<int64_t>(y - destination.y) * source.h) /
                         destination.h);
    for (int x = x0; x < x1; ++x) {
      const int sx = source.x +
          static_cast<int>((static_cast<int64_t>(x - destination.x) * source.w) /
                           destination.w);
      const uint8_t* pixel = rgba +
          (static_cast<size_t>(sy) * source_width + sx) * 4;
      const uint8_t alpha = static_cast<uint8_t>(
          (static_cast<unsigned>(pixel[3]) * tint.a + 127) / 255);
      if (!alpha) continue;
      Blend(x, y, Color(
          static_cast<uint8_t>((static_cast<unsigned>(pixel[0]) * tint.r + 127) /
                               255),
          static_cast<uint8_t>((static_cast<unsigned>(pixel[1]) * tint.g + 127) /
                               255),
          static_cast<uint8_t>((static_cast<unsigned>(pixel[2]) * tint.b + 127) /
                               255),
          alpha));
    }
  }
}

// ---------------------------------------------------------------------- text
HFONT Canvas::FontFor(const FontSpec& spec) {
  auto it = fonts_.find(spec);
  if (it != fonts_.end()) return it->second;

  // A face list rather than one name: the prefix may not have the first
  // choice, and CreateFontA silently substitutes something arbitrary when a
  // face is missing, which is how a UI ends up in Comic Sans on one machine.
  static const char* const kFaces[] = {"Arial Narrow", "Roboto Condensed",
                                       "Segoe UI", "Tahoma", "Arial"};
  HFONT font = nullptr;
  for (const char* face : kFaces) {
    font = CreateFontA(-spec.size, 0, 0, 0, spec.weight, spec.italic ? TRUE : FALSE,
                       FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, face);
    if (font) break;
  }
  fonts_[spec] = font;
  return font;
}

void Canvas::Text(const Box& r, const std::string& text, const FontSpec& font,
                  const Color& c, Align align, bool vcenter) {
  if (!dc_ || text.empty()) return;
  HFONT handle = FontFor(font);
  if (!handle) return;

  HGDIOBJ previous = SelectObject(dc_, handle);
  SetTextColor(dc_, RGB(c.r, c.g, c.b));

  UINT format = DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
  switch (align) {
    case Align::kLeft:   format |= DT_LEFT; break;
    case Align::kCenter: format |= DT_CENTER; break;
    case Align::kRight:  format |= DT_RIGHT; break;
  }
  if (vcenter) format |= DT_VCENTER;

  RECT box = {r.x, r.y, r.right(), r.bottom()};
  const std::wstring wide = ToWide(text);
  DrawTextW(dc_, wide.c_str(), static_cast<int>(wide.size()), &box, format);
  SelectObject(dc_, previous);

  // GDI writes the alpha channel as zero on a 32-bit DIB. Left alone, every
  // glyph would be a transparent hole the moment the buffer reached a
  // swapchain that respects alpha, so the channel is forced back to opaque
  // over the text's own box only.
  const int y1 = std::min(r.bottom(), height_);
  const int x1 = std::min(r.right(), width_);
  for (int y = std::max(0, r.y); y < y1; ++y) {
    uint32_t* row = Row(y);
    for (int x = std::max(0, r.x); x < x1; ++x) row[x] |= 0xff000000u;
  }
}

void Canvas::TextWrap(const Box& r, const std::string& text,
                      const FontSpec& font, const Color& c, Align align) {
  if (!dc_ || text.empty() || r.w <= 0 || r.h <= 0) return;
  HFONT handle = FontFor(font);
  if (!handle) return;

  HGDIOBJ previous = SelectObject(dc_, handle);
  SetTextColor(dc_, RGB(c.r, c.g, c.b));
  UINT format = DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS;
  switch (align) {
    case Align::kLeft: format |= DT_LEFT; break;
    case Align::kCenter: format |= DT_CENTER; break;
    case Align::kRight: format |= DT_RIGHT; break;
  }
  RECT box = {r.x, r.y, r.right(), r.bottom()};
  const std::wstring wide = ToWide(text);
  DrawTextW(dc_, wide.c_str(), static_cast<int>(wide.size()), &box, format);
  SelectObject(dc_, previous);

  const int y1 = std::min(r.bottom(), height_);
  const int x1 = std::min(r.right(), width_);
  for (int y = std::max(0, r.y); y < y1; ++y) {
    uint32_t* row = Row(y);
    for (int x = std::max(0, r.x); x < x1; ++x) row[x] |= 0xff000000u;
  }
}

int Canvas::TextWidth(const std::string& text, const FontSpec& font) {
  if (!dc_ || text.empty()) return 0;
  HFONT handle = FontFor(font);
  if (!handle) return 0;
  HGDIOBJ previous = SelectObject(dc_, handle);
  SIZE size = {};
  const std::wstring wide = ToWide(text);
  GetTextExtentPoint32W(dc_, wide.c_str(), static_cast<int>(wide.size()), &size);
  SelectObject(dc_, previous);
  return size.cx;
}

int Canvas::TextHeight(const FontSpec& font) {
  if (!dc_) return font.size;
  HFONT handle = FontFor(font);
  if (!handle) return font.size;
  HGDIOBJ previous = SelectObject(dc_, handle);
  TEXTMETRICA metrics = {};
  GetTextMetricsA(dc_, &metrics);
  SelectObject(dc_, previous);
  return metrics.tmHeight;
}

// -------------------------------------------------------------------- output
void Canvas::CopyTo(uint8_t* rgba, size_t stride) const {
  if (!bits_ || !rgba) return;
  for (int y = 0; y < height_; ++y) {
    const uint32_t* src = Row(y);
    uint8_t* dst = rgba + static_cast<size_t>(y) * stride;
    for (int x = 0; x < width_; ++x) {
      const uint32_t pixel = src[x];
      dst[0] = static_cast<uint8_t>((pixel >> 16) & 0xff);  // R <- DIB red
      dst[1] = static_cast<uint8_t>((pixel >> 8) & 0xff);   // G
      dst[2] = static_cast<uint8_t>(pixel & 0xff);          // B <- DIB blue
      dst[3] = static_cast<uint8_t>((pixel >> 24) & 0xff);  // A
      dst += 4;
    }
  }
}

}  // namespace dis

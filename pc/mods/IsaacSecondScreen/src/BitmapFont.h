#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace isaacds {

class BitmapFont {
 public:
  bool Load(const wchar_t* descriptor_path, const std::string& page_asset);
  void Clear();
  float Measure(const std::string& text, float height) const;
  void Draw(HDC dc, const std::string& text, float x, float y, float height,
            COLORREF color = RGB(52, 39, 29), bool centered = false) const;
  std::vector<std::string> Wrap(const std::string& text, float height,
                                float maximum_width) const;
  bool ready() const { return line_height_ > 0 && !glyphs_.empty(); }

 private:
  struct Glyph {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    int16_t x_offset = 0;
    int16_t y_offset = 0;
    int16_t x_advance = 0;
  };

  int line_height_ = 0;
  std::unordered_map<uint32_t, Glyph> glyphs_;
  std::string page_asset_;
};

}  // namespace isaacds

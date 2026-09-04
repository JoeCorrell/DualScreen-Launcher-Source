#include "BitmapFont.h"

#include <wtypes.h>
#include <gdiplus.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

#include "AssetCache.h"

namespace isaacds {
namespace {

uint16_t U16(const unsigned char* value) {
  return static_cast<uint16_t>(value[0] | (value[1] << 8));
}

int16_t I16(const unsigned char* value) {
  return static_cast<int16_t>(U16(value));
}

uint32_t U32(const unsigned char* value) {
  return static_cast<uint32_t>(value[0]) |
         (static_cast<uint32_t>(value[1]) << 8) |
         (static_cast<uint32_t>(value[2]) << 16) |
         (static_cast<uint32_t>(value[3]) << 24);
}

std::vector<uint32_t> CodePoints(const std::string& text) {
  std::vector<uint32_t> result;
  for (size_t index = 0; index < text.size();) {
    const unsigned char first = static_cast<unsigned char>(text[index]);
    uint32_t value = first;
    size_t count = 1;
    if ((first & 0xE0) == 0xC0 && index + 1 < text.size()) {
      value = first & 0x1F;
      count = 2;
    } else if ((first & 0xF0) == 0xE0 && index + 2 < text.size()) {
      value = first & 0x0F;
      count = 3;
    } else if ((first & 0xF8) == 0xF0 && index + 3 < text.size()) {
      value = first & 0x07;
      count = 4;
    } else if (first >= 0x80) {
      value = '?';
    }
    bool valid = value != '?';
    for (size_t continuation = 1; continuation < count; ++continuation) {
      const unsigned char byte =
          static_cast<unsigned char>(text[index + continuation]);
      if ((byte & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      value = (value << 6) | (byte & 0x3F);
    }
    result.push_back(valid ? value : static_cast<uint32_t>('?'));
    index += valid ? count : 1;
  }
  return result;
}

}  // namespace

bool BitmapFont::Load(const wchar_t* descriptor_path,
                      const std::string& page_asset) {
  Clear();
  if (!descriptor_path || !*descriptor_path || page_asset.empty())
    return false;
  std::ifstream input(descriptor_path, std::ios::binary);
  if (!input) return false;
  const std::vector<unsigned char> bytes{
      std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  if (bytes.size() < 4 || bytes[0] != 'B' || bytes[1] != 'M' ||
      bytes[2] != 'F' || bytes[3] != 3)
    return false;
  size_t position = 4;
  while (position + 5 <= bytes.size()) {
    const unsigned char type = bytes[position++];
    const uint32_t block_size = U32(bytes.data() + position);
    position += 4;
    if (block_size > bytes.size() - position) return false;
    const size_t end = position + block_size;
    if (type == 2 && block_size >= 2) {
      line_height_ = U16(bytes.data() + position);
    } else if (type == 4) {
      while (position + 20 <= end) {
        const unsigned char* record = bytes.data() + position;
        Glyph glyph;
        const uint32_t id = U32(record);
        glyph.x = U16(record + 4);
        glyph.y = U16(record + 6);
        glyph.width = U16(record + 8);
        glyph.height = U16(record + 10);
        glyph.x_offset = I16(record + 12);
        glyph.y_offset = I16(record + 14);
        glyph.x_advance = I16(record + 16);
        glyphs_[id] = glyph;
        position += 20;
      }
    }
    position = end;
  }
  page_asset_ = page_asset;
  return ready() && ResolveAsset(page_asset_) != nullptr;
}

void BitmapFont::Clear() {
  line_height_ = 0;
  glyphs_.clear();
  page_asset_.clear();
}

float BitmapFont::Measure(const std::string& text, float height) const {
  if (line_height_ <= 0 || height <= 0.0f) return 0.0f;
  const float scale = height / static_cast<float>(line_height_);
  float width = 0.0f;
  for (const uint32_t code_point : CodePoints(text)) {
    const auto found = glyphs_.find(code_point);
    width += static_cast<float>(found == glyphs_.end()
                                    ? line_height_ / 2
                                    : found->second.x_advance) *
             scale;
  }
  return width;
}

void BitmapFont::Draw(HDC dc, const std::string& text, float x, float y,
                      float height, COLORREF color, bool centered) const {
  Gdiplus::Image* page = ResolveAsset(page_asset_);
  if (!dc || !page || line_height_ <= 0 || height <= 0.0f) return;
  const float scale = height / static_cast<float>(line_height_);
  float cursor = centered ? x - Measure(text, height) * 0.5f : x;
  Gdiplus::Graphics graphics(dc);
  graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
  graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
  graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  const Gdiplus::REAL red = GetRValue(color) / 255.0f;
  const Gdiplus::REAL green = GetGValue(color) / 255.0f;
  const Gdiplus::REAL blue = GetBValue(color) / 255.0f;
  Gdiplus::ColorMatrix matrix = {{
      {red, 0, 0, 0, 0},
      {0, green, 0, 0, 0},
      {0, 0, blue, 0, 0},
      {0, 0, 0, 1, 0},
      {0, 0, 0, 0, 1},
  }};
  Gdiplus::ImageAttributes attributes;
  attributes.SetColorMatrix(&matrix);
  for (const uint32_t code_point : CodePoints(text)) {
    const auto found = glyphs_.find(code_point);
    if (found == glyphs_.end()) {
      cursor += height * 0.45f;
      continue;
    }
    const Glyph& glyph = found->second;
    if (glyph.width && glyph.height) {
      const Gdiplus::RectF destination(
          cursor + glyph.x_offset * scale, y + glyph.y_offset * scale,
          std::max(1.0f, glyph.width * scale),
          std::max(1.0f, glyph.height * scale));
      graphics.DrawImage(page, destination, glyph.x, glyph.y, glyph.width,
                         glyph.height, Gdiplus::UnitPixel, &attributes);
    }
    cursor += glyph.x_advance * scale;
  }
}

std::vector<std::string> BitmapFont::Wrap(const std::string& text,
                                          float height,
                                          float maximum_width) const {
  std::vector<std::string> lines;
  std::istringstream words(text);
  std::string word;
  std::string line;
  while (words >> word) {
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (!line.empty() && Measure(candidate, height) > maximum_width) {
      lines.push_back(line);
      line = word;
    } else {
      line = candidate;
    }
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

}  // namespace isaacds

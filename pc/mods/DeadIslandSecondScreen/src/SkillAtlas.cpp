#include "SkillAtlas.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "Log.h"

namespace dis {
namespace {

constexpr int kSkillsDdsResource = 101;
constexpr int kSkillsRectsResource = 102;
constexpr int kItemsDdsResource = 103;
constexpr int kItemsRectsResource = 104;
constexpr uint32_t kDxt1 = 0x31545844u;
constexpr uint32_t kDxt3 = 0x33545844u;
constexpr uint32_t kDxt5 = 0x35545844u;

SkillAtlas g_skill_atlas;
ItemAtlas g_item_atlas;

uint16_t U16(const uint8_t* value) {
  uint16_t out = 0;
  std::memcpy(&out, value, sizeof(out));
  return out;
}

uint32_t U32(const uint8_t* value) {
  uint32_t out = 0;
  std::memcpy(&out, value, sizeof(out));
  return out;
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string Normalize(std::string value) {
  value = Lower(std::move(value));
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    if (std::isalnum(c)) out.push_back(static_cast<char>(c));
  }
  if (out.rfind("skill", 0) == 0) out.erase(0, 5);
  return out;
}

std::string NormalizeItem(std::string value) {
  value = Lower(std::move(value));
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    if (std::isalnum(c)) out.push_back(static_cast<char>(c));
  }
  return out;
}

bool ContainsAny(const std::string& value,
                 std::initializer_list<const char*> needles) {
  for (const char* needle : needles) {
    if (needle && *needle && value.find(needle) != std::string::npos)
      return true;
  }
  return false;
}

bool ResourceBytes(int id, const uint8_t** bytes, size_t* size) {
  if (!bytes || !size) return false;
  *bytes = nullptr;
  *size = 0;
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(&ResourceBytes), &module) || !module) {
    return false;
  }
  HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
  if (!resource) return false;
  HGLOBAL loaded = LoadResource(module, resource);
  if (!loaded) return false;
  const DWORD resource_size = SizeofResource(module, resource);
  const void* resource_bytes = LockResource(loaded);
  if (!resource_bytes || resource_size == 0) return false;
  *bytes = static_cast<const uint8_t*>(resource_bytes);
  *size = resource_size;
  return true;
}

void Rgb565(uint16_t packed, uint8_t* rgb) {
  rgb[0] = static_cast<uint8_t>((((packed >> 11) & 31) * 255 + 15) / 31);
  rgb[1] = static_cast<uint8_t>((((packed >> 5) & 63) * 255 + 31) / 63);
  rgb[2] = static_cast<uint8_t>(((packed & 31) * 255 + 15) / 31);
}

void DecodeColor(const uint8_t* block, bool allow_transparency,
                 uint8_t colors[4][4]) {
  const uint16_t c0 = U16(block);
  const uint16_t c1 = U16(block + 2);
  Rgb565(c0, colors[0]);
  Rgb565(c1, colors[1]);
  colors[0][3] = colors[1][3] = 255;
  if (!allow_transparency || c0 > c1) {
    for (int channel = 0; channel < 3; ++channel) {
      colors[2][channel] = static_cast<uint8_t>(
          (2 * colors[0][channel] + colors[1][channel] + 1) / 3);
      colors[3][channel] = static_cast<uint8_t>(
          (colors[0][channel] + 2 * colors[1][channel] + 1) / 3);
    }
    colors[2][3] = colors[3][3] = 255;
  } else {
    for (int channel = 0; channel < 3; ++channel)
      colors[2][channel] = static_cast<uint8_t>(
          (colors[0][channel] + colors[1][channel]) / 2);
    colors[2][3] = 255;
    colors[3][0] = colors[3][1] = colors[3][2] = colors[3][3] = 0;
  }
}

bool DecodeDds(const uint8_t* data, size_t size, int* width, int* height,
               std::vector<uint8_t>* rgba) {
  if (!data || size < 128 || !width || !height || !rgba ||
      U32(data) != 0x20534444u || U32(data + 4) != 124) {
    return false;
  }
  const int h = static_cast<int>(U32(data + 12));
  const int w = static_cast<int>(U32(data + 16));
  const uint32_t pixel_flags = U32(data + 80);
  const uint32_t fourcc = U32(data + 84);
  if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
  rgba->assign(static_cast<size_t>(w) * h * 4, 0);
  const uint8_t* source = data + 128;
  const size_t source_size = size - 128;

  if ((pixel_flags & 0x4u) == 0) {
    if (source_size < static_cast<size_t>(w) * h * 4) return false;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const uint8_t* src = source + (static_cast<size_t>(y) * w + x) * 4;
        uint8_t* dst = rgba->data() + (static_cast<size_t>(y) * w + x) * 4;
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        dst[3] = src[3];
      }
    }
    *width = w;
    *height = h;
    return true;
  }

  const int bytes_per_block = fourcc == kDxt1 ? 8 : 16;
  if (fourcc != kDxt1 && fourcc != kDxt3 && fourcc != kDxt5) return false;
  const int blocks_x = (w + 3) / 4;
  const int blocks_y = (h + 3) / 4;
  const size_t required = static_cast<size_t>(blocks_x) * blocks_y * bytes_per_block;
  if (source_size < required) return false;

  for (int by = 0; by < blocks_y; ++by) {
    for (int bx = 0; bx < blocks_x; ++bx) {
      const uint8_t* block = source +
          (static_cast<size_t>(by) * blocks_x + bx) * bytes_per_block;
      const uint8_t* color_block = block + (bytes_per_block == 16 ? 8 : 0);
      uint8_t colors[4][4] = {};
      DecodeColor(color_block, fourcc == kDxt1, colors);
      const uint32_t color_indices = U32(color_block + 4);

      uint8_t alpha_palette[8] = {};
      uint64_t alpha_indices = 0;
      uint64_t dxt3_alpha = 0;
      if (fourcc == kDxt5) {
        alpha_palette[0] = block[0];
        alpha_palette[1] = block[1];
        if (alpha_palette[0] > alpha_palette[1]) {
          for (int i = 1; i <= 6; ++i)
            alpha_palette[i + 1] = static_cast<uint8_t>(
                ((7 - i) * alpha_palette[0] + i * alpha_palette[1] + 3) / 7);
        } else {
          for (int i = 1; i <= 4; ++i)
            alpha_palette[i + 1] = static_cast<uint8_t>(
                ((5 - i) * alpha_palette[0] + i * alpha_palette[1] + 2) / 5);
          alpha_palette[6] = 0;
          alpha_palette[7] = 255;
        }
        for (int i = 0; i < 6; ++i)
          alpha_indices |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
      } else if (fourcc == kDxt3) {
        for (int i = 0; i < 8; ++i)
          dxt3_alpha |= static_cast<uint64_t>(block[i]) << (8 * i);
      }

      for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
          const int x = bx * 4 + px;
          const int y = by * 4 + py;
          if (x >= w || y >= h) continue;
          const int pixel = py * 4 + px;
          const int color_index = (color_indices >> (pixel * 2)) & 3;
          uint8_t* dst = rgba->data() +
              (static_cast<size_t>(y) * w + x) * 4;
          dst[0] = colors[color_index][0];
          dst[1] = colors[color_index][1];
          dst[2] = colors[color_index][2];
          if (fourcc == kDxt5) {
            dst[3] = alpha_palette[(alpha_indices >> (pixel * 3)) & 7];
          } else if (fourcc == kDxt3) {
            dst[3] = static_cast<uint8_t>(
                ((dxt3_alpha >> (pixel * 4)) & 15) * 17);
          } else {
            dst[3] = colors[color_index][3];
          }
        }
      }
    }
  }
  *width = w;
  *height = h;
  return true;
}

Box TrimTransparent(const std::vector<uint8_t>& rgba, int atlas_width,
                    int atlas_height, const Box& source) {
  if (rgba.empty() || atlas_width <= 0 || atlas_height <= 0 ||
      source.w <= 0 || source.h <= 0) return source;
  int left = source.right();
  int top = source.bottom();
  int right = source.x - 1;
  int bottom = source.y - 1;
  for (int y = source.y; y < source.bottom() && y < atlas_height; ++y) {
    for (int x = source.x; x < source.right() && x < atlas_width; ++x) {
      const size_t alpha = (static_cast<size_t>(y) * atlas_width + x) * 4 + 3;
      if (alpha >= rgba.size() || rgba[alpha] <= 8) continue;
      left = std::min(left, x);
      top = std::min(top, y);
      right = std::max(right, x);
      bottom = std::max(bottom, y);
    }
  }
  if (right < left || bottom < top) return source;
  left = std::max(source.x, left - 1);
  top = std::max(source.y, top - 1);
  right = std::min(source.right() - 1, right + 1);
  bottom = std::min(source.bottom() - 1, bottom + 1);
  return Box(left, top, right - left + 1, bottom - top + 1);
}

}  // namespace

SkillAtlas& TheSkillAtlas() { return g_skill_atlas; }
ItemAtlas& TheItemAtlas() { return g_item_atlas; }

bool SkillAtlas::ready() { return attempted_ ? !rgba_.empty() : Load(); }

bool SkillAtlas::Load() {
  if (attempted_) return !rgba_.empty();
  attempted_ = true;

  const uint8_t* dds = nullptr;
  size_t dds_size = 0;
  const uint8_t* definitions = nullptr;
  size_t definitions_size = 0;
  if (!ResourceBytes(kSkillsDdsResource, &dds, &dds_size) ||
      !ResourceBytes(kSkillsRectsResource, &definitions, &definitions_size) ||
      !DecodeDds(dds, dds_size, &width_, &height_, &rgba_)) {
    DIS_WARN("[assets] real skill atlas could not be loaded");
    rgba_.clear();
    return false;
  }

  std::string text(reinterpret_cast<const char*>(definitions), definitions_size);
  size_t cursor = 0;
  while ((cursor = text.find("Rect(\"", cursor)) != std::string::npos) {
    const size_t name_begin = cursor + 6;
    const size_t name_end = text.find('"', name_begin);
    if (name_end == std::string::npos) break;
    const std::string name = text.substr(name_begin, name_end - name_begin);
    const size_t comma = text.find(',', name_end);
    const size_t line_end = text.find('\n', name_end);
    if (comma == std::string::npos ||
        (line_end != std::string::npos && comma > line_end)) {
      cursor = name_end + 1;
      continue;
    }
    const char* number = text.c_str() + comma + 1;
    char* end = nullptr;
    int values[4] = {};
    bool valid = true;
    for (int i = 0; i < 4; ++i) {
      values[i] = static_cast<int>(std::strtol(number, &end, 10));
      if (end == number) {
        valid = false;
        break;
      }
      number = end;
      while (*number && (*number == ',' || std::isspace(
              static_cast<unsigned char>(*number)))) ++number;
    }
    if (valid && values[2] > values[0] && values[3] > values[1] &&
        values[0] >= 0 && values[1] >= 0 && values[2] <= width_ &&
        values[3] <= height_) {
      const std::string lower = Lower(name);
      Box region(values[0], values[1], values[2] - values[0],
                 values[3] - values[1]);
      if (lower.rfind("inv_wpn_", 0) == 0 ||
          lower.rfind("inv_slot_", 0) == 0 ||
          lower.rfind("icon_item_", 0) == 0 ||
          lower.rfind("weapon_type_", 0) == 0) {
        region = TrimTransparent(rgba_, width_, height_, region);
      }
      rects_[lower] = region;
      const std::string normalized = Normalize(name);
      if (!normalized.empty() && !normalized_names_.count(normalized))
        normalized_names_[normalized] = lower;
    }
    cursor = name_end + 1;
  }
  DIS_INFO("[assets] loaded native skill atlas %dx%d with %d named icons",
           width_, height_, static_cast<int>(rects_.size()));
  return !rects_.empty();
}

const Box* SkillAtlas::Resolve(const std::string& key) const {
  if (key.empty()) return nullptr;
  std::string candidate = Lower(key);
  const size_t skill = candidate.find("skill_");
  if (skill != std::string::npos) candidate.erase(0, skill);
  const size_t extension = candidate.find(".dds");
  if (extension != std::string::npos) candidate.resize(extension);
  auto exact = rects_.find(candidate);
  if (exact != rects_.end()) return &exact->second;
  if (candidate.rfind("skill_", 0) != 0) {
    exact = rects_.find("skill_" + candidate);
    if (exact != rects_.end()) return &exact->second;
  }
  // Ryder's XML represents each one-rank milestone as a separate R_*1/2/3
  // skill, while the shipped atlas intentionally gives that family one icon
  // without the R_ prefix or rank suffix. Try the authored family spellings
  // from most specific to least specific (R_Fury33 correctly finds Fury3
  // before Fury, for example).
  if (candidate.rfind("r_", 0) == 0) {
    std::string family = candidate.substr(2);
    while (!family.empty()) {
      exact = rects_.find("skill_" + family);
      if (exact != rects_.end()) return &exact->second;
      if (!std::isdigit(static_cast<unsigned char>(family.back()))) break;
      family.pop_back();
    }
  }
  const auto normalized = normalized_names_.find(Normalize(candidate));
  if (normalized == normalized_names_.end()) return nullptr;
  exact = rects_.find(normalized->second);
  return exact == rects_.end() ? nullptr : &exact->second;
}

bool SkillAtlas::Draw(Canvas& canvas, const Box& destination,
                      const std::string& key, const Color& tint) {
  if (!ready()) return false;
  const Box* source = Resolve(key);
  if (!source) return false;
  canvas.BlitRgba(destination, rgba_.data(), width_, height_, *source, tint);
  return true;
}

bool ItemAtlas::ready() { return attempted_ ? !rgba_.empty() : Load(); }

bool ItemAtlas::Load() {
  if (attempted_) return !rgba_.empty();
  attempted_ = true;

  const uint8_t* dds = nullptr;
  size_t dds_size = 0;
  const uint8_t* definitions = nullptr;
  size_t definitions_size = 0;
  if (!ResourceBytes(kItemsDdsResource, &dds, &dds_size) ||
      !ResourceBytes(kItemsRectsResource, &definitions, &definitions_size) ||
      !DecodeDds(dds, dds_size, &width_, &height_, &rgba_)) {
    DIS_WARN("[assets] native HUD/item atlas could not be loaded");
    rgba_.clear();
    return false;
  }

  std::string text(reinterpret_cast<const char*>(definitions), definitions_size);
  size_t cursor = 0;
  while ((cursor = text.find("Rect(\"", cursor)) != std::string::npos) {
    const size_t name_begin = cursor + 6;
    const size_t name_end = text.find('"', name_begin);
    if (name_end == std::string::npos) break;
    const std::string name = text.substr(name_begin, name_end - name_begin);
    const size_t comma = text.find(',', name_end);
    const size_t line_end = text.find('\n', name_end);
    if (comma == std::string::npos ||
        (line_end != std::string::npos && comma > line_end)) {
      cursor = name_end + 1;
      continue;
    }

    const char* number = text.c_str() + comma + 1;
    char* end = nullptr;
    int values[4] = {};
    bool valid = true;
    for (int i = 0; i < 4; ++i) {
      values[i] = static_cast<int>(std::strtol(number, &end, 10));
      if (end == number) {
        valid = false;
        break;
      }
      number = end;
      while (*number && (*number == ',' || std::isspace(
              static_cast<unsigned char>(*number)))) ++number;
    }
    if (valid && values[2] > values[0] && values[3] > values[1] &&
        values[0] >= 0 && values[1] >= 0 && values[2] <= width_ &&
        values[3] <= height_) {
      const std::string lower = Lower(name);
      Box region(values[0], values[1], values[2] - values[0],
                 values[3] - values[1]);
      if (lower.rfind("inv_wpn_", 0) == 0 ||
          lower.rfind("inv_slot_", 0) == 0 ||
          lower.rfind("icon_item_", 0) == 0 ||
          lower.rfind("weapon_type_", 0) == 0) {
        region = TrimTransparent(rgba_, width_, height_, region);
      }
      rects_[lower] = region;
      const std::string normalized = NormalizeItem(name);
      if (!normalized.empty() && !normalized_names_.count(normalized))
        normalized_names_[normalized] = lower;

      if (lower.rfind("inv_wpn_", 0) == 0) {
        std::string stem = lower.substr(8);
        std::string alias = NormalizeItem(stem);
        if (!alias.empty()) weapon_aliases_.emplace_back(alias, lower);

        // The atlas contains authored A/B visual variants. A translated
        // display name normally omits that suffix, so use the first matching
        // family only when the native presenter did not expose the exact key.
        if (stem.size() > 2 && stem[stem.size() - 2] == '_' &&
            stem.back() >= 'a' && stem.back() <= 'd') {
          stem.resize(stem.size() - 2);
          alias = NormalizeItem(stem);
          if (!alias.empty()) weapon_aliases_.emplace_back(alias, lower);
        } else if ((stem.rfind("doubleblade", 0) == 0 ||
                    stem.rfind("sickle", 0) == 0) &&
                   !stem.empty() && std::isdigit(
                       static_cast<unsigned char>(stem.back()))) {
          stem.pop_back();
          alias = NormalizeItem(stem);
          if (!alias.empty()) weapon_aliases_.emplace_back(alias, lower);
        }
      }
    }
    cursor = name_end + 1;
  }

  std::sort(weapon_aliases_.begin(), weapon_aliases_.end(),
            [](const auto& left, const auto& right) {
              return left.first.size() > right.first.size();
            });
  DIS_INFO("[assets] loaded native item atlas %dx%d with %d named regions",
           width_, height_, static_cast<int>(rects_.size()));
  return !rects_.empty();
}

const Box* ItemAtlas::Resolve(const std::string& texture_key,
                              const std::string& display_name) const {
  const auto find = [&](const char* name) -> const Box* {
    const auto it = rects_.find(name);
    return it == rects_.end() ? nullptr : &it->second;
  };
  const auto find_string = [&](const std::string& name) -> const Box* {
    const auto it = rects_.find(name);
    return it == rects_.end() ? nullptr : &it->second;
  };
  const auto find_normalized = [&](const std::string& value) -> const Box* {
    const std::string normalized = NormalizeItem(value);
    const auto alias = normalized_names_.find(normalized);
    return alias == normalized_names_.end() ? nullptr
                                             : find_string(alias->second);
  };

  std::string texture = Lower(texture_key);
  if (const Box* exact = find_string(texture)) return exact;
  if (const Box* exact = find_normalized(texture)) return exact;

  // Runtime values can include an atlas/material prefix. Retain any exact
  // authored rectangle embedded in that value before considering aliases.
  for (const auto& entry : rects_) {
    if (texture.find(entry.first) != std::string::npos) return &entry.second;
  }

  const std::string words = NormalizeItem(texture_key + " " + display_name);
  if (const Box* exact = find_normalized(display_name)) return exact;
  for (const auto& alias : weapon_aliases_) {
    if (alias.first.size() >= 4 && words.find(alias.first) != std::string::npos) {
      if (const Box* match = find_string(alias.second)) return match;
    }
  }

  // Display names include quality adjectives and are localized, while some
  // older weapon-family keys use reversed words. These are conservative game
  // family fallbacks; they never override a concrete native texture key.
  if (ContainsAny(words, {"universalaxe"})) return find("inv_wpn_axeuniversal");
  if (ContainsAny(words, {"splittingaxe", "splittingaxe"}))
    return find("inv_wpn_axesplitting");
  if (ContainsAny(words, {"fireaxe"})) return find("inv_wpn_fireaxe_a");
  if (ContainsAny(words, {"hatchet"})) return find("inv_wpn_axehatchet");
  if (ContainsAny(words, {"baseballbat"})) return find("inv_wpn_baseballbat_a");
  if (ContainsAny(words, {"paddle"})) return find("inv_wpn_paddle_a");
  if (ContainsAny(words, {"bentpipe", "metalpipe", "pipe"}))
    return find("inv_wpn_pipebent");
  if (ContainsAny(words, {"stungun", "tasergun", "tasergun"}))
    return find("inv_wpn_teaser_gun");
  if (ContainsAny(words, {"medkit", "firstaid", "healthkit"}))
    return find("inv_wpn_medkit");
  if (ContainsAny(words, {"molotov"})) return find("inv_wpn_molotov");
  if (ContainsAny(words, {"grenade"})) return find("inv_wpn_grenade");
  if (ContainsAny(words, {"bottle", "alcohol"})) return find("inv_wpn_bottle");

  if (ContainsAny(words, {"ammo", "ammunition"})) return find("icon_item_ammo");
  if (ContainsAny(words, {"pistol", "rifle", "shotgun", "revolver",
                          "firearm", "gun"})) return find("icon_item_firearm");
  if (ContainsAny(words, {"knife", "blade", "machete", "sword", "katana",
                          "cleaver", "sharp"})) return find("icon_item_sharp");
  if (ContainsAny(words, {"hammer", "wrench", "bat", "club", "crowbar",
                          "blunt", "cudgel", "mace"})) return find("icon_item_blunt");
  if (ContainsAny(words, {"bomb", "throwable", "explosive"}))
    return find("icon_item_throwable");
  if (ContainsAny(words, {"craft", "part", "material"}))
    return find("icon_item_craft_part");
  return find("icon_item_other");
}

bool ItemAtlas::Draw(Canvas& canvas, const Box& destination,
                     const std::string& texture_key,
                     const std::string& display_name, const Color& tint) {
  if (!ready()) return false;
  const Box* source = Resolve(texture_key, display_name);
  if (!source) return false;
  Box fitted = destination;
  const double source_aspect = static_cast<double>(source->w) / source->h;
  const double destination_aspect = static_cast<double>(destination.w) /
                                    std::max(1, destination.h);
  if (destination_aspect > source_aspect) {
    fitted.w = std::max(1, static_cast<int>(destination.h * source_aspect));
    fitted.x = destination.x + (destination.w - fitted.w) / 2;
  } else {
    fitted.h = std::max(1, static_cast<int>(destination.w / source_aspect));
    fitted.y = destination.y + (destination.h - fitted.h) / 2;
  }
  canvas.BlitRgba(fitted, rgba_.data(), width_, height_, *source, tint);
  return true;
}

}  // namespace dis

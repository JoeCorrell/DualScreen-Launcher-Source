#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Canvas.h"

namespace dis {

// The game's own skills.dds atlas and generated rectangle table, embedded as
// Win32 resources so the proxy remains a one-file install. Nothing is sampled
// from an engine-owned GPU resource and no pointer survives a level change.
class SkillAtlas {
 public:
  bool Draw(Canvas& canvas, const Box& destination, const std::string& key,
            const Color& tint = Color(255, 255, 255));
  bool ready();

 private:
  bool Load();
  const Box* Resolve(const std::string& key) const;

  bool attempted_ = false;
  int width_ = 0;
  int height_ = 0;
  std::vector<uint8_t> rgba_;
  std::unordered_map<std::string, Box> rects_;
  std::unordered_map<std::string, std::string> normalized_names_;
};

SkillAtlas& TheSkillAtlas();

// Dead Island ships its inventory silhouettes in the native hud.dds atlas.
// Most named weapons have their own authored rectangle; a few deliberately
// share a family image. This reader keeps that original distinction and only
// falls back to a category icon when no matching weapon/item rectangle exists.
class ItemAtlas {
 public:
  bool Draw(Canvas& canvas, const Box& destination,
            const std::string& texture_key, const std::string& display_name,
            const Color& tint = Color(255, 255, 255));
  bool ready();

 private:
  bool Load();
  const Box* Resolve(const std::string& texture_key,
                     const std::string& display_name) const;

  bool attempted_ = false;
  int width_ = 0;
  int height_ = 0;
  std::vector<uint8_t> rgba_;
  std::unordered_map<std::string, Box> rects_;
  std::unordered_map<std::string, std::string> normalized_names_;
  std::vector<std::pair<std::string, std::string>> weapon_aliases_;
};

ItemAtlas& TheItemAtlas();

}  // namespace dis

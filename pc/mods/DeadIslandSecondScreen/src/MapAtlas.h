// Lazy loader for the real Dead Island Definitive Edition world-map tiles.
//
// The five outdoor maps in common_textures_0_pc.rpack are stored as 16x16
// grids of 256px DXT1 textures.  Loading their 64px mip directly gives the
// second screen a crisp 1024x1024 map while reading only 512 KiB per area.

#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "Canvas.h"

namespace dis {

class MapAtlas {
 public:
  static constexpr int kWidth = 1024;
  static constexpr int kHeight = 1024;

  // Loads the atlas matching `level_name`, if this is one of the shipped
  // outdoor areas. Results (including a validated failure) are cached, so a
  // page can call this every frame without doing filesystem work every frame.
  bool EnsureLoaded(const std::string& level_name);

  // Draws the whole native map without changing its aspect ratio. The image is
  // centred and letterboxed when `destination` is not square.
  bool Draw(Canvas& canvas, const Box& destination,
            const std::string& level_name,
            const Color& tint = Color(255, 255, 255, 255));

  // Draws a cropped, aspect-correct viewport. `zoom` is 1 for the largest
  // source rectangle that fills the destination, and larger values zoom in.
  // Centre coordinates are normalized atlas coordinates and are clamped so no
  // edge outside the map is sampled.
  bool DrawViewport(Canvas& canvas, const Box& destination,
                    const std::string& level_name, float center_u,
                    float center_v, float zoom,
                    const Color& tint = Color(255, 255, 255, 255));

  // Same crop as DrawViewport, but never performs pack I/O or tile decoding.
  // Status uses this path so entering the world cannot stall Present while
  // the default-page minimap is drawn. It returns false until the full Map
  // page (or an explicit preload) has populated the cache.
  bool DrawViewportIfReady(Canvas& canvas, const Box& destination,
                           const std::string& level_name, float center_u,
                           float center_v, float zoom,
                           const Color& tint = Color(255, 255, 255, 255));

  bool SupportsLevel(const std::string& level_name) const;
  std::string LastError(const std::string& level_name) const;

 private:
  enum class LoadState : uint8_t { kUntried, kReady, kFailed };

  struct TileRef {
    uint64_t data_offset = 0;
    uint32_t data_size = 0;
    bool valid = false;
  };

  struct AreaCache {
    std::array<TileRef, 256> tiles{};
    unsigned tile_count = 0;
    LoadState state = LoadState::kUntried;
    std::vector<uint8_t> rgba;
    std::string error;
  };

  static int AreaIndex(const std::string& level_name);
  bool EnsureLoadedLocked(int area);
  bool BuildIndexLocked();
  bool LoadAreaLocked(int area);
  void FailAreaLocked(int area, const char* reason);

  mutable std::mutex mutex_;
  bool index_attempted_ = false;
  bool index_ready_ = false;
  std::wstring pack_path_;
  std::string index_error_;
  std::array<AreaCache, 5> areas_{};
};

MapAtlas& TheMapAtlas();

}  // namespace dis

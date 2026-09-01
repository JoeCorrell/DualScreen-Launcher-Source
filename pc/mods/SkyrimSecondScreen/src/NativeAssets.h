#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../../DeadIslandSecondScreen/src/Canvas.h"

namespace dis {

// CPU copies of a deliberately small set of assets from Skyrim's own
// Interface BSA. They are embedded in the proxy at build time so rendering
// never calls Scaleform, Inventory3DManager, or the game's D3D device.
class NativeAssets {
 public:
  struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    bool valid() const {
      return width > 0 && height > 0 &&
             rgba.size() == static_cast<size_t>(width) * height * 4;
    }
  };

  bool ready();
  bool DrawFrame(Canvas& canvas, const Box& destination,
                 const Color& tint = Color(190, 204, 199, 255));
  bool DrawMeter(Canvas& canvas, const Box& destination, float ratio,
                 const Color& tint);
  bool DrawMarker(Canvas& canvas, const Box& destination, const Color& tint,
                  float heading_radians);
  bool DrawLocationMarker(Canvas& canvas, const Box& destination,
                          uint16_t marker_type, bool discovered,
                          const Color& tint);
  bool LocationMarkerDimensions(uint16_t marker_type, int* width,
                                int* height) const;
  bool DrawQuestMarker(Canvas& canvas, const Box& destination,
                       uint16_t marker_type, const Color& tint);
  bool DrawPlayerSetMarker(Canvas& canvas, const Box& destination,
                           const Color& tint);
  bool DrawDiamond(Canvas& canvas, const Box& destination, const Color& tint);
  bool DrawArrow(Canvas& canvas, const Box& destination, bool right,
                 const Color& tint);
  bool DrawPageIcon(Canvas& canvas, const Box& destination, int page_index,
                    const Color& tint = Color(255, 255, 255, 255));
  bool DrawWorldMap(Canvas& canvas, const Box& destination,
                    const Box& source,
                    const Color& tint = Color(255, 255, 255, 255));
  bool DrawWorldMap3D(Canvas& canvas, const Box& destination,
                      const Box& source,
                      const Color& tint = Color(255, 255, 255, 255));
  // Projects an atlas coordinate through the same terrain camera used by
  // DrawWorldMap3D. Map markers must use this instead of a flat UV mapping or
  // they visibly slide away from mountains as the perspective changes.
  bool ProjectWorldMapPoint(const Box& destination, const Box& source,
                            float atlas_x, float atlas_y,
                            int* screen_x, int* screen_y) const;
  bool WorldMapSize(int* width, int* height);

 private:
  bool Load();
  bool LoadWorldMap();
  bool LoadTerrainHeight();
  float SampleTerrainHeight(float atlas_x, float atlas_y) const;
  bool Draw(Canvas& canvas, const Box& destination, const Image& image,
            const Color& tint);

  bool attempted_ = false;
  bool world_map_attempted_ = false;
  bool terrain_height_attempted_ = false;
  Image frame_;
  Image meter_track_;
  Image meter_fill_;
  Image marker_;
  Image marker_scratch_;
  Image location_markers_[59];
  Image location_markers_undiscovered_[59];
  Image quest_marker_;
  Image quest_door_marker_;
  Image multiple_quest_marker_;
  Image player_set_marker_;
  Image diamond_;
  Image arrow_left_;
  Image arrow_right_;
  Image page_icons_[5];
  Image world_map_;
  Image projected_world_map_;
  std::vector<uint16_t> terrain_height_;
  int terrain_width_ = 0;
  int terrain_height_rows_ = 0;
  float terrain_minimum_ = 0.0f;
  float terrain_maximum_ = 0.0f;
  Box projected_source_;
  int projected_width_ = 0;
  int projected_height_ = 0;
  uint64_t last_map_render_log_ = 0;
  uint32_t map_renders_since_log_ = 0;
};

NativeAssets& TheNativeAssets();

}  // namespace dis

#pragma once

#include <algorithm>
#include <cmath>

namespace fo3ds {

// One immutable transform describes both the map image crop and every overlay
// placed on it. Keeping the player, door/location markers and raster on this
// same transform prevents UI pan state from leaking into game coordinates.
struct MapProjection {
  float left = 0.0f;
  float top = 0.0f;
  float right = 1.0f;
  float bottom = 1.0f;
};

inline bool BuildMapProjection(float min_x, float max_y, float max_x,
                               float min_y, float image_width,
                               float image_height, int viewport_width,
                               int viewport_height, float anchor_x,
                               float anchor_y, float pan_x, float pan_y,
                               float zoom, MapProjection* output) {
  if (!output || !(max_x > min_x) || !(max_y > min_y) ||
      !(image_width > 0.0f) || !(image_height > 0.0f) ||
      viewport_width <= 0 || viewport_height <= 0 ||
      !std::isfinite(anchor_x) || !std::isfinite(anchor_y) ||
      !std::isfinite(pan_x) || !std::isfinite(pan_y) ||
      !std::isfinite(zoom))
    return false;

  const float destination_aspect =
      viewport_width / static_cast<float>(viewport_height);
  const float image_aspect = image_width / image_height;
  float span_u = 1.0f;
  float span_v = 1.0f;
  if (image_aspect > destination_aspect)
    span_u = destination_aspect / image_aspect;
  else
    span_v = image_aspect / destination_aspect;
  zoom = std::clamp(zoom, 1.0f, 6.0f);
  span_u /= zoom;
  span_v /= zoom;

  const float world_width = max_x - min_x;
  const float world_height = max_y - min_y;
  float center_u = (anchor_x + pan_x - min_x) / world_width;
  float center_v = (max_y - (anchor_y + pan_y)) / world_height;
  center_u = std::clamp(center_u, span_u * 0.5f,
                        1.0f - span_u * 0.5f);
  center_v = std::clamp(center_v, span_v * 0.5f,
                        1.0f - span_v * 0.5f);
  output->left = center_u - span_u * 0.5f;
  output->right = center_u + span_u * 0.5f;
  output->top = center_v - span_v * 0.5f;
  output->bottom = center_v + span_v * 0.5f;
  return output->right > output->left && output->bottom > output->top;
}

inline bool ProjectMapPoint(float min_x, float max_y, float max_x,
                            float min_y, const MapProjection& projection,
                            int viewport_x, int viewport_y,
                            int viewport_width, int viewport_height,
                            float world_x, float world_y, int* screen_x,
                            int* screen_y) {
  if (!screen_x || !screen_y || !(max_x > min_x) || !(max_y > min_y) ||
      !(projection.right > projection.left) ||
      !(projection.bottom > projection.top) || viewport_width <= 0 ||
      viewport_height <= 0 || !std::isfinite(world_x) ||
      !std::isfinite(world_y))
    return false;
  const float u = (world_x - min_x) / (max_x - min_x);
  const float v = (max_y - world_y) / (max_y - min_y);
  if (u < projection.left || u > projection.right ||
      v < projection.top || v > projection.bottom)
    return false;
  *screen_x = viewport_x + static_cast<int>(std::lround(
      (u - projection.left) /
      (projection.right - projection.left) * viewport_width));
  *screen_y = viewport_y + static_cast<int>(std::lround(
      (v - projection.top) /
      (projection.bottom - projection.top) * viewport_height));
  return true;
}

inline bool ProjectMapPointInset(float min_x, float max_y, float max_x,
                                 float min_y,
                                 const MapProjection& projection,
                                 int viewport_x, int viewport_y,
                                 int viewport_width, int viewport_height,
                                 int inset_x, int inset_y, float world_x,
                                 float world_y, int* screen_x,
                                 int* screen_y) {
  if (inset_x < 0 || inset_y < 0 || inset_x * 2 >= viewport_width ||
      inset_y * 2 >= viewport_height ||
      !ProjectMapPoint(min_x, max_y, max_x, min_y, projection, viewport_x,
                       viewport_y, viewport_width, viewport_height, world_x,
                       world_y, screen_x, screen_y))
    return false;
  return *screen_x >= viewport_x + inset_x &&
         *screen_x <= viewport_x + viewport_width - inset_x &&
         *screen_y >= viewport_y + inset_y &&
         *screen_y <= viewport_y + viewport_height - inset_y;
}

}  // namespace fo3ds

// Complete lower-screen presentation built from live DXHR data.
//
// GDI is used only for the opaque plates, typography and fine Deus Ex-style
// rails. NativeUiRenderer composites the game's own pickup/UI textures over
// those plates afterward. Keeping geometry here gives rendering and touch one
// source of truth.

#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

#include "GameActions.h"
#include "NativeMap.h"
#include "NativeUiRenderer.h"
#include "SmartGlass.h"

namespace dxs {

enum class CustomHitKind : uint8_t {
  kNone,
  kInventoryItem,
  kWeapon,
  kQuickSlot,
  kInventoryGrid,
  kInventoryPrevious,
  kInventoryNext,
  kInventorySort,
  kDiscard,
  kStatusAugmentation,
  kAugmentation,
  kAugmentationNode,
  kAugmentationPrevious,
  kAugmentationNext,
  kAugmentationPurchase,
  kObjective,
  kObjectivePrevious,
  kObjectiveNext,
  kObjectiveToggleTracking,
  kMedia,
  kMediaPrevious,
  kMediaNext,
  kMap,
  kMapZoomIn,
  kMapZoomOut,
  kMapCenter,
  kMapFloorPrevious,
  kMapFloorNext,
};

struct CustomHit {
  CustomHitKind kind = CustomHitKind::kNone;
  int value = -1;
  int secondary = -1;
  uint32_t pickup_id = 0xFFFFFFFFu;
};

struct CustomUiIcon {
  enum class Kind : uint8_t {
    kMapRaster,
    kCircle,
    kDiamond,
    kPlayerArrow,
    kLock,
    kPickup,
    kTexture,
  } kind = Kind::kPickup;
  uint32_t id = 0;
  int secondary = 0;
  bool rotated = false;
  float rotation_degrees = 0.0f;
  NativeUiRect rect;
  NativeUiUvRect uv;
  NativeUiColor color;
};

struct CustomUiState {
  int selected_item = -1;
  int drag_item = -1;
  float drag_x = 0.0f;
  float drag_y = 0.0f;
  bool drag_active = false;
  uint64_t animation_ms = 0;
  int selected_aug = 0;
  // -1 selects the augmentation family/base unlock. Non-negative values are
  // native node indices from that family's DTP relation graph.
  int selected_aug_node = -1;
  int selected_objective = 0;
  int selected_media = 0;
  int inventory_page = 0;
  // The lower screen presents every pickup as one touch-sized card even
  // though DXHR's native inventory uses variable-size packed rectangles.
  // Keeping a separate visual slot order makes drag/drop predictable without
  // invoking Container::Move on occupied native cells.
  std::vector<int> inventory_slots;
  int augmentation_page = 0;
  int objective_page = 0;
  int media_page = 0;
  float map_zoom = 3.0f;
  float map_pan_x = 0.0f;
  float map_pan_y = 0.0f;
  // True while the authored map transform is locked to Jensen. A manual drag
  // clears this until the center control is tapped, so the UI can communicate
  // whether incoming pose updates will move the map under the player's finger.
  bool map_follow_player = true;
};

struct CustomUiFrame {
  std::vector<CustomUiIcon> icons;
  std::vector<CustomHit> hits;
  std::vector<RECT> hit_rects;
  uintptr_t inventory_identity = 0;
  uint32_t inventory_revision = 0;
  RECT inventory_grid = {};
  int inventory_grid_width = 0;
  int inventory_grid_height = 0;
  int inventory_card_columns = 0;
  int inventory_card_rows = 0;
  int inventory_card_size = 0;
  int inventory_card_gap = 0;
  int inventory_card_page = 0;
  int inventory_unlocked_slots = 0;
};

void DrawCustomUi(HDC dc, int width, int height, int header, int page,
                  const InventorySnapshot& inventory,
                  const SmartGlassSnapshot& smartglass,
                  const NativeMapSnapshot& native_map,
                  const CustomUiState& state, CustomUiFrame* frame);

// Clamp map interaction state against the same aspect-fitted raster geometry
// used by DrawCustomUi. Callers can safely use this after a gesture, zoom,
// resize, floor change, or redraw without duplicating the map transform.
void ClampCustomMapView(int width, int height, int header,
                        const NativeMapSnapshot& native_map, float* zoom,
                        float* pan_x, float* pan_y);

// Centers the authored map transform on the live normalized player position,
// clamped so no blank raster edge can ever be exposed.
void CenterCustomMapOnPlayer(int width, int height, int header,
                             const NativeMapSnapshot& native_map,
                             float* zoom, float* pan_x, float* pan_y);

CustomHit HitCustomUi(const CustomUiFrame& frame, int x, int y);

}  // namespace dxs

#pragma once

#include <cstdint>
#include <vector>

#include "Bridge.h"
#include "../../DeadIslandSecondScreen/src/Canvas.h"

namespace dis {

class Ui {
 public:
  bool Create(int width, int height);
  bool Render(const Snapshot& state);
  bool RenderLoading();
  bool Pointer(unsigned message, float nx, float ny);
  bool Tap(float nx, float ny);
  bool Hold(float nx, float ny);
  bool Pinch(float nx, float ny, float factor);
  void CancelPointer();
  void MarkDirty() {
    dirty_ = true;
    loading_dirty_ = true;
  }

  const Canvas& canvas() const { return canvas_; }
  Page page() const { return page_; }
  Box CharacterPreviewBounds() const;
  bool character_preview_active() const { return page_ == Page::kStatus; }
  float character_rotation_degrees() const {
    return character_rotation_degrees_;
  }
  float character_zoom() const { return character_zoom_; }
  uint64_t character_control_revision() const {
    return character_control_revision_;
  }
 private:
  enum class DragTarget : uint8_t {
    kNone,
    kCharacter,
    kMap,
    kEquipment,
    kInventory,
    kMagic,
    kQuests,
    kQuestObjectives
  };

  void DrawFrame(const Snapshot& state);
  void DrawNavigation();
  void DrawDashboard(const Snapshot& state);
  void DrawInventory(const Snapshot& state);
  void DrawMap(const Snapshot& state);
  void DrawMagic(const Snapshot& state);
  void DrawQuests(const Snapshot& state);
  void DrawFavorites(const Snapshot& state);
  void DrawPanel(const Box& box, bool selected = false);
  void DrawHeader(const Box& box, const char* title);
  void DrawMeter(const Box& box, const char* label, float current, float maximum,
                 const Color& color);
  void ClampMapPan();
  void SelectPage(Page page, const char* source);
  bool Queue(CommandType type, uint32_t form, int index = -1);
  bool QueueMapMarkerRefresh(const Snapshot& state);
  bool QueueMapPreview(const Snapshot& state, float pan_x, float pan_y,
                       float zoom_delta, uint32_t extra_flags = 0);
  void BeginHotbarAssignment(uint32_t form_id);
  void CancelHotbarAssignment();
  uint64_t VisualFingerprint(const Snapshot& state) const;

  int CategoryFor(uint32_t form_type) const;
  std::vector<int> FilteredItems(const Snapshot& state) const;
  int MagicCategoryFor(const MagicState& magic) const;
  std::vector<int> FilteredMagic(const Snapshot& state) const;

  Canvas canvas_;
  // Hit-testing must use exactly the snapshot whose pixels are on screen.
  // Reading the newest bridge snapshot during a tap can target a different
  // inventory row when Skyrim reorders its arrays between frames.
  Snapshot rendered_state_ = {};
  bool have_rendered_state_ = false;
  Page page_ = Page::kStatus;
  uint64_t rendered_sequence_ = UINT64_MAX;
  uint64_t rendered_fingerprint_ = UINT64_MAX;
  bool dirty_ = true;
  bool loading_visible_ = false;
  bool loading_dirty_ = true;
  int category_ = 0;
  int magic_category_ = 0;
  int selected_item_ = -1;
  int selected_magic_ = -1;
  int selected_quest_ = -1;
  uint32_t selected_item_form_ = 0;
  uint32_t selected_magic_form_ = 0;
  uint32_t selected_quest_form_ = 0;
  uint32_t hotbar_assignment_form_ = 0;
  int equipment_scroll_ = 0;
  int inventory_scroll_ = 0;
  int magic_scroll_ = 0;
  int quest_scroll_ = 0;
  int quest_objective_scroll_ = 0;
  // The lower map owns this camera completely. Its anchor is captured once
  // per world and is never derived from the live gameplay camera afterwards.
  float map_zoom_ = 1.0f;
  float map_pan_x_ = 0.0f;
  float map_pan_y_ = 0.0f;
  float map_anchor_x_ = 0.0f;
  float map_anchor_y_ = 0.0f;
  uint32_t map_anchor_world_id_ = 0;
  bool map_anchor_valid_ = false;
  bool pointer_down_ = false;
  bool pointer_moved_ = false;
  float down_x_ = 0.0f;
  float down_y_ = 0.0f;
  float last_x_ = 0.0f;
  float last_y_ = 0.0f;
  float drag_scroll_pixels_ = 0.0f;
  DragTarget drag_target_ = DragTarget::kNone;
  float character_rotation_degrees_ = 0.0f;
  float character_zoom_ = 1.0f;
  uint64_t character_control_revision_ = 1;
  uint64_t last_character_tap_ms_ = 0;
  uint64_t last_map_tap_ms_ = 0;
  uint64_t last_map_marker_refresh_request_ms_ = 0;
  uint64_t map_control_revision_ = 0;
};

Ui& TheUi();
// Returns false only when the appropriate bounded command lane rejected the
// command. Callers that create pending work can then roll that state back.
bool PushUiCommand(const Command& command);
bool CopyLatestSnapshot(Snapshot* out);

using DrawNativeItemPreviewFn = bool(*)(Canvas& canvas, const Box& destination,
                                        uint32_t form_id,
                                        uint64_t world_epoch);
using RequestNativeItemPreviewFn = void(*)(uint32_t form_id,
                                           uint64_t world_epoch,
                                           bool priority,
                                           uint64_t composition_revision);
using BeginNativeItemPreviewCompositionFn = void(*)(
    uint64_t world_epoch, uint64_t composition_revision);
void SetNativeItemPreviewCallbacks(DrawNativeItemPreviewFn draw,
                                   RequestNativeItemPreviewFn request,
                                   BeginNativeItemPreviewCompositionFn begin);
using DrawNativeCharacterPreviewFn = bool(*)(Canvas& canvas,
                                              const Box& destination);
void SetNativeCharacterPreviewCallback(DrawNativeCharacterPreviewFn draw);
using DrawNativeMapPreviewFn = bool(*)(Canvas& canvas,
                                       const Box& destination, float zoom,
                                       float pan_x, float pan_y);
void SetNativeMapPreviewCallback(DrawNativeMapPreviewFn draw);

}  // namespace dis

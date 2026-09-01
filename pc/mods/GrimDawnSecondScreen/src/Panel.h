#pragma once

#include <cstddef>
#include <cstdint>

#include <windows.h>

namespace gdds {

struct PanelFrameInfo {
  HWND window = nullptr;
  int width = 0;
  int height = 0;
  uint32_t revision = 0;
  bool character_stage = false;
  RECT character_bounds = {};
};

bool StartPanel(HMODULE module, unsigned short input_port);
HWND PanelWindow();
bool CopyPanelFrame(uint32_t* pixels, size_t pixel_capacity,
                    PanelFrameInfo* info);
void SetPanelGpuActive(bool active);

}  // namespace gdds

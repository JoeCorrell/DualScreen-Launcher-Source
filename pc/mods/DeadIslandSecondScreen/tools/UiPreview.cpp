// Renders the panel interface to BMP files, without the game.
//
// The interface is authored for a 1240x1080 screen that is attached to a
// handheld running a Windows game under translation, which is an awkward thing
// to iterate against: every look at it otherwise costs a build, a push, a
// launch and a save load. This links the real Canvas, Ui and Pages -- not a
// mock of them -- and writes what they draw to disk, so a layout mistake is
// found in a second rather than after a five-minute round trip.
//
// It deliberately does not stub the game side. Game and GameState are the real
// classes, left unbound, which is exactly the state the panel is in before a
// save is loaded. So this previews the honest empty states as well as the
// chrome, and if those look wrong here they look wrong on the device.
//
//   g++ -std=c++17 -O2 -DUNICODE -D_UNICODE -municode ^
//       tools/UiPreview.cpp src/Canvas.cpp src/Ui.cpp src/Pages.cpp ^
//       src/Game.cpp src/GameState.cpp src/Rtti.cpp src/Log.cpp src/Mem.cpp ^
//       -o build/ui-preview.exe -lgdi32
//   build\ui-preview.exe out-dir

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../src/Game.h"
#include "../src/GameState.h"
#include "../src/NativeUi.h"
#include "../src/Ui.h"

namespace {

bool WriteBmp(const std::string& path, const dis::Canvas& canvas) {
  const int width = canvas.width();
  const int height = canvas.height();
  const size_t stride = static_cast<size_t>(width) * 4;
  std::vector<uint8_t> rgba(stride * height);
  canvas.CopyTo(rgba.data(), stride);

  BITMAPFILEHEADER file = {};
  BITMAPINFOHEADER info = {};
  file.bfType = 0x4D42;
  file.bfOffBits = sizeof(file) + sizeof(info);
  file.bfSize = static_cast<DWORD>(file.bfOffBits + rgba.size());
  info.biSize = sizeof(info);
  info.biWidth = width;
  info.biHeight = -height;  // top-down, matching the canvas
  info.biPlanes = 1;
  info.biBitCount = 32;
  info.biCompression = BI_RGB;

  FILE* out = std::fopen(path.c_str(), "wb");
  if (!out) return false;
  std::fwrite(&file, sizeof(file), 1, out);
  std::fwrite(&info, sizeof(info), 1, out);
  // CopyTo emits RGBA; a BMP wants BGRA, so the same swap runs back the other
  // way. Cheaper than teaching CopyTo about two output orders.
  for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
    const uint8_t pixel[4] = {rgba[i + 2], rgba[i + 1], rgba[i], rgba[i + 3]};
    std::fwrite(pixel, 1, 4, out);
  }
  std::fclose(out);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";
  CreateDirectoryA(dir.c_str(), nullptr);

  dis::Interface& ui = dis::TheInterface();
  if (!ui.Create(dis::theme::kScreenW, dis::theme::kScreenH)) {
    std::fprintf(stderr, "could not create the canvas\n");
    return 1;
  }

  dis::Game game;
  dis::GameState state;
  state.SetPreviewData();
  dis::TheNativeUi().SetPreviewData();

  // Exercise the same thumb-sized tab hit boxes used by the live panel. A
  // rendered preview can look perfect while an accidental shell-layout edit
  // makes its pages unreachable; keep that regression in this fast harness.
  constexpr int kTabCount = static_cast<int>(dis::Page::kCount);
  constexpr int kMargin = 30;
  constexpr int kGap = 12;
  const int tab_width = (dis::theme::kScreenW - 2 * kMargin -
                         (kTabCount - 1) * kGap) / kTabCount;
  for (int tab = 0; tab < kTabCount; ++tab) {
    const int x = kMargin + tab * (tab_width + kGap) + tab_width / 2;
    const int y = 62;
    if (!ui.OnTouchDown(x, y) || !ui.OnTouchUp(x, y) ||
        static_cast<int>(ui.page()) != tab) {
      std::fprintf(stderr, "tab hit test failed at index %d\n", tab);
      return 2;
    }
  }
  std::printf("tab hit test: %d/%d passed\n", kTabCount, kTabCount);

  struct Shot { dis::Page page; const char* name; };
  const Shot shots[] = {
      {dis::Page::kStatus,    "status"},
      {dis::Page::kInventory, "inventory"},
      {dis::Page::kMap,       "map"},
      {dis::Page::kSkills,    "skills"},
  };

  int written = 0;
  for (const Shot& shot : shots) {
    ui.SetPage(shot.page);
    ui.Render(game, state);
    const std::string path = dir + "/ui-" + shot.name + ".bmp";
    if (WriteBmp(path, ui.canvas())) {
      std::printf("wrote %s\n", path.c_str());
      ++written;
    } else {
      std::fprintf(stderr, "failed to write %s\n", path.c_str());
    }
  }
  std::printf("%d page(s) at %dx%d\n", written, ui.canvas().width(),
              ui.canvas().height());
  return written == 0 ? 1 : 0;
}

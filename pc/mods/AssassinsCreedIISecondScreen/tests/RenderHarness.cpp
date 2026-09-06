#include "AnimusUi.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>
using namespace ac2ds;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    Gdiplus::GdiplusStartupInput startup; ULONG_PTR token{};
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) return 3;
    {
        UINT count{}, size{}; Gdiplus::GetImageEncodersSize(&count, &size);
        std::vector<unsigned char> bytes(size); auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(bytes.data());
        Gdiplus::GetImageEncoders(count, size, encoders); CLSID png{}; bool found = false;
        for (UINT i = 0; i < count; ++i) if (!wcscmp(encoders[i].MimeType, L"image/png")) { png = encoders[i].Clsid; found = true; }
        if (!found) return 4;
        std::filesystem::path root(argv[1]); std::filesystem::create_directories(root);
        Snapshot s; s.status = L"Awaiting synchronization";
        // No fabricated live values or static replacement Ezio image in this harness.
        for (int p = 0; p < 6; ++p) {
            Gdiplus::Bitmap image(Width, Height, PixelFormat32bppARGB);
            { Gdiplus::Graphics g(&image); UiState ui; ui.page = Page(p); DrawAnimus(g, s, ui, 1200); }
            auto path = root / (std::to_wstring(p) + L".png");
            assert(image.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok);
        }
        StateStore store; UiState ui;
        ui.TouchInput({TouchKind::Tap, .43f, .11f, 0}, s, store, 1000); assert(ui.page == Page::Inventory);
        ui.TouchInput({TouchKind::Next}, s, store, 1001); assert(ui.page == Page::Missions);
        ui.selected = 12; s.generation = 2; ui.Synchronize(s); assert(!ui.selected && !ui.scroll);
        // Synthetic test fixtures exercise bounds only; they never enter the DLL.
        ui.page = Page::Inventory; s.items.resize(100); ui.Synchronize(s);
        ui.scroll = ui.maximumScroll - 1; ui.velocity = 8000;
        ui.Animate(.05f, 1200); assert(ui.scroll == ui.maximumScroll && ui.velocity == 0);
        const auto before = ui.scroll;
        ui.TouchInput({TouchKind::Down, .9f, .5f}, s, store, 1201);
        ui.TouchInput({TouchKind::Move, .9f, .3f}, s, store, 1202);
        assert(ui.scroll == before); // Details never scroll the inventory grid.
        ui.page = Page::Database; s.database.resize(100);
        s.database[0].unlocked = true; s.database[0].category = L"Test";
        ui.Synchronize(s); assert(ui.maximumScroll == 0 && ui.scroll == 0);
        s.database[0].id = 42; s.database[0].title = L"Scrolling test fixture";
        for (int i = 0; i < 120; ++i) s.database[0].text += L"Long description used only to exercise the scrolling bounds.\n";
        s.capabilities = Database; ui.selected = 42;
        {
            Gdiplus::Bitmap surface(Width, Height, PixelFormat32bppARGB); Gdiplus::Graphics g(&surface);
            DrawAnimus(g, s, ui, 1250); assert(ui.maximumDetailScroll > 1000);
        }
        ui.TouchInput({TouchKind::Down, .8f, .8f}, s, store, 1260);
        ui.TouchInput({TouchKind::Move, .8f, .7f}, s, store, 1261);
        assert(ui.detailScroll > 100 && ui.scroll == 0);
        ui.TouchInput({TouchKind::Cancel}, s, store, 1262);
        ui.page = Page::Progress; s.statistics.resize(24); ui.Synchronize(s); assert(ui.maximumScroll > 1000);
        ui.page = Page::Map; s.map = std::make_shared<ac2ds::Image>();
        s.mapX = .65f; s.mapY = .4f; ui.map.zoom = 4; ui.following = true;
        ui.TouchInput({TouchKind::Down, .5f, .5f}, s, store, 1300);
        ui.TouchInput({TouchKind::Move, .5f, .5f}, s, store, 1301);
        assert(std::abs(ui.map.x - .65f) < .0001f && std::abs(ui.map.y - .4f) < .0001f);
    }
    Gdiplus::GdiplusShutdown(token); std::cout << "Six Animus pages rendered; navigation and generation reset passed\n";
}

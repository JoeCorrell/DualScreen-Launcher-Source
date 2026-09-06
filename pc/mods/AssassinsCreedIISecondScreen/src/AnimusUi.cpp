#include "AnimusUi.h"
#include <array>
#include <cmath>
#include <cwchar>

namespace ac2ds {
using namespace Gdiplus;
namespace {
const Color Ink(255, 42, 46, 47), Muted(255, 106, 119, 123), Red(255, 137, 24, 31);
const Color Rule(255, 180, 191, 192), Paper(240, 247, 248, 246);
constexpr const wchar_t* Tabs[]{L"MAP", L"EQUIPMENT", L"INVENTORY", L"MISSIONS", L"DATABASE", L"PROGRESS"};
RectF R(float x, float y, float w, float h) { return {x, y, w, h}; }
RectF MapArea(const Snapshot& s) {
    RectF area = R(34, 238, 866, 724);
    if (s.map && s.map->width > 0 && s.map->height > 0) {
        float scale = std::min(area.Width / s.map->width, area.Height / s.map->height);
        float w = scale * s.map->width, h = scale * s.map->height;
        return R(area.X + (area.Width - w) / 2, area.Y + (area.Height - h) / 2, w, h);
    }
    return area;
}
std::vector<const Item*> EquipmentGroups(const Snapshot& s) {
    std::vector<const Item*> groups;
    for (const auto& item : s.items) {
        if (item.category.empty()) continue;
        auto found = std::find_if(groups.begin(), groups.end(), [&](const Item* current) { return current->category == item.category; });
        if (found == groups.end()) groups.push_back(&item);
        else if ((item.equipped && !(*found)->equipped) || (item.owned && !(*found)->owned)) *found = &item;
    }
    return groups;
}
void Fill(Graphics& g, RectF r, Color color) { SolidBrush b(color); g.FillRectangle(&b, r); }
void Line(Graphics& g, float x, float y, float xx, float yy, Color color = Rule, float width = 1) {
    Pen p(color, width); g.DrawLine(&p, x, y, xx, yy);
}
void Box(Graphics& g, RectF r, bool selected = false) {
    Fill(g, r, Paper); Pen p(selected ? Red : Rule, selected ? 2.f : 1.f); g.DrawRectangle(&p, r);
    Line(g, r.X, r.Y, r.X + 22, r.Y, selected ? Red : Color(255, 126, 142, 146), 3);
}
void Text(Graphics& g, const std::wstring& text, RectF r, float size = 24,
          Color color = Ink, bool bold = false, bool center = false) {
    FontFamily condensed(L"Arial Narrow");
    const FontFamily* family = condensed.GetLastStatus() == Ok ? &condensed : FontFamily::GenericSansSerif();
    Font font(family, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(color); StringFormat format;
    format.SetTrimming(StringTrimmingEllipsisWord);
    if (center) format.SetAlignment(StringAlignmentCenter);
    g.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, r, &format, &brush);
}
void ImageAt(Graphics& g, const std::shared_ptr<const Image>& image, RectF r) {
    if (!image || image->width <= 0 || image->height <= 0 || image->width > 4096 || image->height > 4096 ||
        image->bgra.size() != size_t(image->width) * size_t(image->height)) return;
    Bitmap bitmap(image->width, image->height, image->width * 4, PixelFormat32bppARGB,
        reinterpret_cast<BYTE*>(const_cast<uint32_t*>(image->bgra.data())));
    float scale = std::min(r.Width / image->width, r.Height / image->height);
    RectF target(r.X + (r.Width - scale * image->width) / 2, r.Y + (r.Height - scale * image->height) / 2,
        image->width * scale, image->height * scale);
    g.DrawImage(&bitmap, target);
}
void ScrollingText(Graphics& g, const std::wstring& text, RectF area, UiState& ui) {
    Font font(FontFamily::GenericSansSerif(), 24, FontStyleRegular, UnitPixel);
    StringFormat format; RectF extent;
    INT length = INT(std::min(text.size(), size_t(65536)));
    g.MeasureString(text.c_str(), length, &font, R(0, 0, area.Width, 100000), &format, &extent);
    ui.maximumDetailScroll = std::max(0.f, extent.Height - area.Height);
    ui.detailScroll = std::clamp(ui.detailScroll, 0.f, ui.maximumDetailScroll);
    auto saved = g.Save(); g.SetClip(area);
    SolidBrush ink(Ink);
    g.DrawString(text.c_str(), length, &font, R(area.X, area.Y - ui.detailScroll, area.Width,
        std::max(area.Height, extent.Height + 2)), &format, &ink);
    g.Restore(saved);
    if (ui.maximumDetailScroll > 0) {
        float thumb = std::max(32.f, area.Height * area.Height / (area.Height + ui.maximumDetailScroll));
        Fill(g, R(area.GetRight() + 5, area.Y + (area.Height - thumb) * ui.detailScroll / ui.maximumDetailScroll, 3, thumb), Red);
    }
}
std::wstring Number(std::optional<double> n, bool percent = false) {
    if (!n || !std::isfinite(*n)) return L"—";
    wchar_t buffer[64]{}; std::swprintf(buffer, 64, percent ? L"%.1f%%" : L"%.0f", *n); return buffer;
}
void Bar(Graphics& g, RectF r, std::optional<double> value, std::optional<double> total) {
    Fill(g, r, Color(255, 214, 218, 218));
    if (value && total && std::isfinite(*value) && std::isfinite(*total) && *total > 0)
        Fill(g, R(r.X, r.Y, r.Width * float(std::clamp(*value / *total, 0., 1.)), r.Height), Red);
    for (int i = 1; i < 6; ++i) Line(g, r.X + r.Width * i / 6, r.Y - 3, r.X + r.Width * i / 6, r.Y + r.Height + 3, Paper, 3);
}
void Dna(Graphics& g, RectF r, double phase) {
    float previousA = 0, previousB = 0, previousY = 0;
    for (int i = 0; i <= 64; ++i) {
        float y = r.Y + r.Height * i / 64;
        float wave = float(std::sin(i * .19 + phase));
        float a = r.X + r.Width * (.5f + .42f * wave), b = r.X + r.Width * (.5f - .42f * wave);
        if (i) { Line(g, previousA, previousY, a, y, Red, 2); Line(g, previousB, previousY, b, y, Rule, 2); }
        if (i % 3 == 0) Line(g, a, y, b, y, Color(160, 165, 175, 177));
        previousA = a; previousB = b; previousY = y;
    }
}
void Unavailable(Graphics& g, RectF r, const wchar_t* domain) {
    Text(g, domain, R(r.X + 20, r.Y + r.Height / 2 - 43, r.Width - 40, 38), 28, Muted, true, true);
    Text(g, L"Awaiting synchronization", R(r.X + 20, r.Y + r.Height / 2 + 2, r.Width - 40, 40), 23, Muted, false, true);
}
void Top(Graphics& g, const Snapshot& s, const UiState& ui, uint64_t now) {
    LinearGradientBrush background(Point(0, 0), Point(1240, 1080), Color(255, 253, 254, 251), Color(255, 213, 224, 226));
    g.FillRectangle(&background, 0, 0, Width, Height);
    for (int i = -1000; i < 2000; i += 156) {
        Line(g, float(i), 0, float(i + 540), 1080, Color(60, 255, 255, 255));
        Line(g, 0, float(i), 1240, float(i + 300), Color(50, 139, 159, 165));
    }
    // Sparse construction fragments are original vector geometry, never a game-state visualization.
    for (int i = 0; i < 16; ++i) {
        float x = float((i * 277 + 33) % 1190), y = float((i * 173 + 181) % 990);
        Line(g, x, y, x + 17, y, Color(85, 155, 174, 179)); Line(g, x, y, x, y + 17, Color(85, 155, 174, 179));
    }
    Text(g, L"ANIMUS 2.0", R(34, 18, 300, 46), 33, Ink, true);
    Text(g, L"EZIO AUDITORE  /  GENETIC MEMORY", R(425, 28, 775, 35), 21, Muted, false);
    Line(g, 32, 76, 1208, 76);
    for (int i = 0; i < 6; ++i) {
        bool active = int(ui.page) == i, pressed = ui.pressedTab == i && now < ui.pressedUntil;
        RectF tab = R(32.f + 196 * i, 88, 196, 66);
        Fill(g, tab, pressed ? Color(255, 172, 38, 45) : active ? Red : Color(185, 225, 230, 230));
        Text(g, Tabs[i], R(tab.X, 107, tab.Width, 40), 25, active || pressed ? Color::White : Ink, true, true);
    }
    Text(g, s.location.empty() ? Tabs[int(ui.page)] : s.location, R(38, 178, 910, 44), 29, Ink, true);
    Text(g, L"◈  ANIMUS", R(34, 1028, 230, 30), 21, Muted, true);
    Text(g, s.status, R(284, 1028, 920, 40), 21, Muted);
    Line(g, 32, 1011, 1208, 1011);
}
void DrawMap(Graphics& g, const Snapshot& s, const UiState& ui) {
    Box(g, R(34, 238, 866, 724)); RectF area = MapArea(s);
    if (!(s.capabilities & Map) || !s.map) Unavailable(g, area, L"CITY MAP");
    else {
        auto saved = g.Save(); g.SetClip(area);
        auto viewport = ui.map;
        if (ui.following && s.mapX && s.mapY) { viewport.x = *s.mapX; viewport.y = *s.mapY; viewport.Clamp(); }
        const auto& map = s.map;
        if (map->width > 0 && map->height > 0 && map->width <= 4096 && map->height <= 4096 &&
            map->bgra.size() == size_t(map->width) * size_t(map->height)) {
            Bitmap bitmap(map->width, map->height, map->width * 4, PixelFormat32bppARGB,
                reinterpret_cast<BYTE*>(const_cast<uint32_t*>(map->bgra.data())));
            g.DrawImage(&bitmap, area, (viewport.x - .5f / viewport.zoom) * map->width,
                (viewport.y - .5f / viewport.zoom) * map->height,
                map->width / viewport.zoom, map->height / viewport.zoom, UnitPixel);
        }
        auto point = [&](float x, float y) { return PointF(area.X + ((x - viewport.x) * viewport.zoom + .5f) * area.Width,
            area.Y + ((y - viewport.y) * viewport.zoom + .5f) * area.Height); };
        for (const auto& marker : s.markers) {
            if (!std::isfinite(marker.x) || !std::isfinite(marker.y)) continue;
            PointF p = point(marker.x, marker.y); SolidBrush fill(marker.id == ui.selected ? Red : Ink);
            g.FillEllipse(&fill, p.X - 7, p.Y - 7, 14.f, 14.f);
        }
        if (s.mapX && s.mapY && s.heading && std::isfinite(*s.mapX) && std::isfinite(*s.mapY) && std::isfinite(*s.heading)) {
            auto center = point(*s.mapX, *s.mapY); PointF arrow[3];
            for (int i = 0; i < 3; ++i) { float a = *s.heading + (i ? (i == 1 ? 2.4f : -2.4f) : 0.f);
                arrow[i] = {center.X + std::sin(a) * 20, center.Y - std::cos(a) * 20}; }
            SolidBrush brush(Red); g.FillPolygon(&brush, arrow, 3);
        }
        g.Restore(saved);
    }
    Box(g, R(922, 238, 284, 320)); ImageAt(g, s.preview, R(930, 246, 268, 302));
    Box(g, R(922, 578, 284, 266)); Text(g, L"CURRENT OBJECTIVE", R(938, 596, 252, 50), 23, Ink, true);
    std::wstring details = s.objective.empty() ? L"Awaiting synchronization" : s.objective;
    for (const auto& marker : s.markers) if (marker.id == ui.selected) details = marker.name + L"\n\n" + marker.description;
    Text(g, details, R(938, 650, 252, 176), 23);
    Box(g, R(922, 866, 284, 78), ui.following); Text(g, L"CENTER ON EZIO", R(930, 886, 268, 44), 23, Red, true, true);
}
void ItemCard(Graphics& g, const Item& item, RectF r, bool selected) {
    Box(g, r, selected); ImageAt(g, item.icon, R(r.X + 12, r.Y + 8, r.Width - 24, r.Height - 55));
    Text(g, item.name, R(r.X + 9, r.GetBottom() - 44, r.Width - 18, 41), 21);
    if (item.quantity) Text(g, std::to_wstring(*item.quantity), R(r.GetRight() - 57, r.Y + 7, 46, 32), 23, Ink, true, true);
    if (item.equipped) { Fill(g, R(r.X + 2, r.GetBottom() - 4, r.Width - 4, 3), Red); }
}
void DrawInventory(Graphics& g, const Snapshot& s, UiState& ui, bool equipment, uint64_t now) {
    if (equipment) {
        Box(g, R(326, 238, 578, 724));
        if (s.preview) ImageAt(g, s.preview, R(337, 249, 556, 702));
        else Unavailable(g, R(326, 238, 578, 724), L"EZIO AUDITORE");
        auto groups = EquipmentGroups(s); auto saved = g.Save(); g.SetClip(R(34, 238, 1172, 724));
        for (size_t i = 0; i < groups.size(); ++i) {
            float x = i % 2 ? 928.f : 34.f, y = 238.f + float(i / 2) * 220 - ui.scroll;
            if (y + 208 < 238 || y > 962) continue;
            Text(g, groups[i]->category, R(x, y, 278, 35), 24, Ink, true);
            ItemCard(g, *groups[i], R(x, y + 38, 278, 166), ui.selected == groups[i]->id);
        }
        g.Restore(saved);
        return;
    }
    RectF grid = R(34, 238, 786, 724); auto saved = g.Save(); g.SetClip(grid);
    for (size_t i = 0; i < s.items.size(); ++i) {
        RectF slot = R(34.f + float(i % 5) * 157, 238.f + float(i / 5) * 164 - ui.scroll, 145, 152);
        if (slot.GetBottom() >= grid.Y && slot.Y < grid.GetBottom()) ItemCard(g, s.items[i], slot, ui.selected == s.items[i].id);
    }
    g.Restore(saved);
    if (!(s.capabilities & Inventory)) Unavailable(g, grid, L"POSSESSIONS");
    Box(g, R(842, 238, 364, 724));
    const Item* item = nullptr; for (const auto& candidate : s.items) if (candidate.id == ui.selected) item = &candidate;
    if (!item) { Text(g, L"Select an item to inspect", R(866, 551, 316, 70), 26, Muted, false, true); return; }
    ImageAt(g, item->icon, R(870, 258, 308, 230));
    Text(g, item->name, R(866, 508, 316, 78), 30, Ink, true);
    ScrollingText(g, item->description + L"\n\n" + item->details, R(866, 592, 306, 270), ui);
    if (item->actions && item->owned) { Box(g, R(866, 878, 316, 60), true);
        bool pressed = ui.pressedAction == item->id && now < ui.pressedActionUntil;
        if (pressed) Fill(g, R(867, 879, 314, 58), Red);
        Text(g, (item->actions & ActionBit(ActionKind::Equip)) ? L"EQUIP" :
            (item->actions & ActionBit(ActionKind::Use)) ? L"USE" : L"SELECT", R(866, 891, 316, 40), 25,
            pressed ? Color::White : Red, true, true); }
}
void DrawMissions(Graphics& g, const Snapshot& s, const UiState& ui, uint64_t now) {
    Box(g, R(34, 238, 282, 724)); Dna(g, R(99, 308, 152, 588), now * .00018);
    Text(g, L"DNA SEQUENCES", R(52, 254, 246, 42), 25, Ink, true, true);
    auto saved = g.Save(); g.SetClip(R(338, 238, 364, 724));
    const Memory* selected = nullptr; int row = 0;
    for (const auto& memory : s.memories) {
        if (!memory.unlocked && !memory.exposed) continue;
        RectF r = R(338, 238.f + row++ * 120 - ui.scroll, 364, 106);
        if (memory.id == ui.selected || (!ui.selected && memory.active)) selected = &memory;
        if (r.GetBottom() < 238 || r.Y > 962) continue;
        Box(g, r, memory.id == ui.selected || memory.active);
        Text(g, memory.sequence, R(r.X + 16, r.Y + 10, 332, 28), 19, Muted);
        Text(g, memory.unlocked ? memory.name : L"MEMORY LOCKED", R(r.X + 16, r.Y + 42, 332, 55), 26,
            memory.unlocked ? (memory.active ? Red : Ink) : Muted, true);
    }
    g.Restore(saved); Box(g, R(724, 238, 482, 724));
    if (!(s.capabilities & Missions)) { Unavailable(g, R(724, 238, 482, 724), L"GENETIC MEMORY"); return; }
    if (selected) {
        if (!selected->unlocked) { Text(g, L"MEMORY LOCKED", R(748, 550, 434, 70), 30, Muted, true, true); return; }
        Text(g, selected->name, R(748, 266, 434, 92), 32, Ink, true);
        Text(g, selected->description, R(748, 372, 434, 184), 25);
        Text(g, L"CURRENT OBJECTIVE", R(748, 594, 434, 42), 23, Ink, true);
        Text(g, selected->objective, R(748, 646, 434, 140), 26);
        Text(g, L"SYNCHRONIZATION  " + Number(selected->synchronization, true), R(748, 812, 434, 42), 23, Ink, true);
        Bar(g, R(748, 862, 434, 12), selected->synchronization, 100);
        if (selected->replayable) {
            Box(g, R(748, 890, 434, 60), true);
            bool pressed = ui.pressedAction == selected->id && now < ui.pressedActionUntil;
            if (pressed) Fill(g, R(749, 891, 432, 58), Red);
            Text(g, L"REPLAY MEMORY", R(748, 906, 434, 36), 25, pressed ? Color::White : Red, true, true);
        }
    }
}
void DrawDatabase(Graphics& g, const Snapshot& s, UiState& ui) {
    std::vector<std::wstring> categories;
    for (const auto& entry : s.database) if (entry.unlocked &&
        std::find(categories.begin(), categories.end(), entry.category) == categories.end()) categories.push_back(entry.category);
    Box(g, R(34, 238, 246, 724));
    ui.maximumCategoryScroll = std::max(0.f, float(categories.size()) * 58 - 692);
    ui.categoryScroll = std::clamp(ui.categoryScroll, 0.f, ui.maximumCategoryScroll);
    auto categoryClip = g.Save(); g.SetClip(R(46, 254, 222, 692));
    for (size_t i = 0; i < categories.size(); ++i) {
        RectF r = R(46, 254.f + float(i) * 58 - ui.categoryScroll, 222, 54); bool active = ui.category == categories[i];
        if (r.GetBottom() < 254 || r.Y > 946) continue;
        if (active) Fill(g, r, Red);
        Text(g, categories[i], R(r.X + 10, r.Y + 12, r.Width - 20, 40), 24, active ? Color::White : Ink, true);
    }
    g.Restore(categoryClip);
    const DatabaseEntry* selected = nullptr; int row = 0;
    auto saved = g.Save(); g.SetClip(R(302, 238, 414, 724));
    for (const auto& entry : s.database) {
        if (!entry.unlocked || (!ui.category.empty() && entry.category != ui.category)) continue;
        if (entry.id == ui.selected) selected = &entry;
        RectF r = R(302, 238.f + row++ * 116 - ui.scroll, 414, 104);
        if (r.GetBottom() < 238 || r.Y > 962) continue;
        Box(g, r, entry.id == ui.selected); ImageAt(g, entry.illustration, R(308, r.Y + 6, 104, 92));
        Text(g, entry.title, R(424, r.Y + 18, 274, 80), 26, Ink, true);
    }
    g.Restore(saved); Box(g, R(738, 238, 468, 724));
    if (!(s.capabilities & Database)) Unavailable(g, R(738, 238, 468, 724), L"HISTORICAL ARCHIVE");
    else if (selected) { ImageAt(g, selected->illustration, R(758, 258, 428, 252));
        Text(g, selected->title, R(762, 530, 420, 82), 30, Ink, true);
        ScrollingText(g, selected->text, R(762, 626, 410, 312), ui); }
}
void DrawProgress(Graphics& g, const Snapshot& s, const UiState& ui, uint64_t now) {
    Text(g, L"OVERALL SYNCHRONIZATION  " + Number(s.synchronization, true), R(52, 260, 1136, 58), 35, Ink, true);
    Bar(g, R(52, 338, 1136, 18), s.synchronization, 100);
    Dna(g, R(526, 418, 188, 516), now * .00018);
    auto saved = g.Save(); g.SetClip(R(34, 410, 1172, 544));
    for (size_t i = 0; i < s.statistics.size(); ++i) {
        auto& stat = s.statistics[i]; RectF r = R(i % 2 ? 756.f : 52.f, 422.f + float(i / 2) * 172 - ui.scroll, 432, 142);
        if (r.GetBottom() < 410 || r.Y > 954) continue;
        Box(g, r); Text(g, stat.name, R(r.X + 20, r.Y + 14, r.Width - 40, 38), 28, Ink, true);
        Text(g, Number(stat.value, stat.percentage) + (stat.maximum && !stat.percentage ? L" / " + Number(stat.maximum) : L""),
            R(r.X + 20, r.Y + 60, r.Width - 40, 42), 27);
        Bar(g, R(r.X + 20, r.Y + 116, r.Width - 40, 10), stat.value, stat.percentage ? 100 : stat.maximum);
    }
    g.Restore(saved);
    if (!(s.capabilities & Progress)) Text(g, L"Completion records awaiting synchronization", R(190, 960, 860, 36), 25, Muted, false, true);
}
}

void DrawAnimus(Graphics& g, const Snapshot& s, UiState& ui, uint64_t now) {
    g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    Top(g, s, ui, now);
    switch (ui.page) {
        case Page::Map: DrawMap(g, s, ui); break;
        case Page::Equipment: DrawInventory(g, s, ui, true, now); break;
        case Page::Inventory: DrawInventory(g, s, ui, false, now); break;
        case Page::Missions: DrawMissions(g, s, ui, now); break;
        case Page::Database: DrawDatabase(g, s, ui); break;
        case Page::Progress: DrawProgress(g, s, ui, now); break;
    }
}
void UiState::Synchronize(const Snapshot& s) {
    if (s.generation != generation) {
        generation = s.generation; selected = 0; scroll = 0; velocity = 0;
        category.clear(); map = {}; dragging = false; following = true;
        detailScroll = maximumDetailScroll = categoryScroll = maximumCategoryScroll = 0;
    }
    size_t rows = 0;
    float height = 0;
    if (page == Page::Inventory) { rows = (s.items.size() + 4) / 5; height = 164; }
    if (page == Page::Equipment) { rows = (EquipmentGroups(s).size() + 1) / 2; height = 220; }
    if (page == Page::Missions) { rows = std::count_if(s.memories.begin(), s.memories.end(), [](const Memory& m) { return m.unlocked || m.exposed; }); height = 120; }
    if (page == Page::Database) { rows = std::count_if(s.database.begin(), s.database.end(), [&](const DatabaseEntry& e) {
        return e.unlocked && (category.empty() || e.category == category); }); height = 116; }
    maximumScroll = std::max(0.f, float(rows) * height - 724);
    if (page == Page::Progress) maximumScroll = std::max(0.f, float((s.statistics.size() + 1) / 2) * 172 - 532);
    scroll = std::clamp(scroll, 0.f, maximumScroll);
}
bool UiState::Animate(float elapsed, uint64_t now) {
    if (!std::isfinite(elapsed) || elapsed < 0) return false;
    elapsed = std::min(elapsed, .05f);
    bool changed = std::abs(velocity) > 1;
    if (!dragging && changed) {
        float& offset = dragRegion == 3 ? detailScroll : dragRegion == 4 ? categoryScroll : scroll;
        float maximum = dragRegion == 3 ? maximumDetailScroll : dragRegion == 4 ? maximumCategoryScroll : maximumScroll;
        offset = std::clamp(offset + velocity * elapsed, 0.f, maximum);
        velocity *= std::exp(-9 * elapsed);
        if (offset == 0 || offset == maximum) velocity = 0;
    }
    if (pressedTab >= 0 && now >= pressedUntil) { pressedTab = -1; changed = true; }
    if (pressedAction && now >= pressedActionUntil) { pressedAction = 0; changed = true; }
    return changed;
}
void UiState::TouchInput(const Touch& t, const Snapshot& s, StateStore& store, uint64_t now) {
    Synchronize(s); float x = t.x * Width, y = t.y * Height;
    auto setPage = [&](int p) { page = Page((p + 6) % 6); scroll = 0; selected = 0; velocity = 0;
        detailScroll = maximumDetailScroll = categoryScroll = maximumCategoryScroll = 0;
        category.clear(); dragging = false; Synchronize(s); };
    if (t.kind == TouchKind::Next || t.kind == TouchKind::Previous) { setPage(int(page) + (t.kind == TouchKind::Next ? 1 : -1)); return; }
    if (t.kind == TouchKind::Cancel) { dragging = false; velocity = 0; return; }
    if ((t.kind == TouchKind::Tap || t.kind == TouchKind::Down) && y >= 88 && y < 154 && x >= 32 && x < 1208) {
        pressedTab = int((x - 32) / 196); pressedUntil = now + 160;
        if (t.kind == TouchKind::Tap) setPage(pressedTab);
        return;
    }
    auto inside = [&](float left, float right) { return x >= left && x <= right && y >= 238 && y <= 962; };
    auto mapArea = MapArea(s);
    bool inMap = mapArea.Contains(x, y);
    auto captureFollow = [&] {
        if (following && s.mapX && s.mapY) { map.x = *s.mapX; map.y = *s.mapY; map.Clamp(); }
        following = false;
    };
    if (t.kind == TouchKind::Down) {
        dragRegion = page == Page::Map && inMap ? 1 :
            ((page == Page::Inventory && inside(34, 820)) || (page == Page::Missions && inside(338, 702)) ||
             (page == Page::Database && inside(302, 716)) || (page == Page::Progress && y >= 410 && y <= 954)) ? 2 :
             (page == Page::Equipment && (inside(34, 312) || inside(928, 1206))) ? 2 :
             ((page == Page::Database && inside(738, 1206) && y >= 626) ||
              (page == Page::Inventory && inside(842, 1206) && y >= 592 && y <= 862)) ? 3 :
             (page == Page::Database && inside(34, 280)) ? 4 : 0;
        dragging = dragRegion != 0; velocity = 0; lastX = x; lastY = y; return;
    }
    if (t.kind == TouchKind::Move && dragging) {
        if (dragRegion == 1 && s.map) { captureFollow(); map.Pan((x - lastX) / mapArea.Width, (y - lastY) / mapArea.Height); }
        else if (dragRegion >= 2) {
            float change = lastY - y;
            float& offset = dragRegion == 3 ? detailScroll : dragRegion == 4 ? categoryScroll : scroll;
            float maximum = dragRegion == 3 ? maximumDetailScroll : dragRegion == 4 ? maximumCategoryScroll : maximumScroll;
            offset = std::clamp(offset + change, 0.f, maximum);
            velocity = change * 25;
        }
        lastX = x; lastY = y; return;
    }
    if (t.kind == TouchKind::Up) { dragging = false; return; }
    if (t.kind == TouchKind::Pinch && page == Page::Map && inMap && s.map) {
        captureFollow(); map.Pinch(t.value, (x - mapArea.X) / mapArea.Width, (y - mapArea.Y) / mapArea.Height); dragging = false; return;
    }
    if (t.kind != TouchKind::Tap) return;
    if (page == Page::Map) {
        if (x >= 922 && y >= 866 && y <= 944) { following = true; selected = 0; return; }
        if (!inMap) return;
        MapViewport view = map; if (following && s.mapX && s.mapY) { view.x = *s.mapX; view.y = *s.mapY; view.Clamp(); }
        float nearest = 28; selected = 0;
        for (const auto& marker : s.markers) { float mx = mapArea.X + ((marker.x - view.x) * view.zoom + .5f) * mapArea.Width;
            float my = mapArea.Y + ((marker.y - view.y) * view.zoom + .5f) * mapArea.Height;
            float distance = std::hypot(x - mx, y - my); if (distance < nearest) { nearest = distance; selected = marker.id; } }
    } else if (page == Page::Inventory) {
        if (x >= 34 && x < 819 && y >= 238 && y <= 962) {
            size_t i = size_t((y - 238 + scroll) / 164) * 5 + size_t((x - 34) / 157);
            if (i < s.items.size()) { selected = s.items[i].id; detailScroll = maximumDetailScroll = velocity = 0; }
        } else if (x >= 866 && x <= 1182 && y >= 878 && y <= 938) {
            for (const auto& item : s.items) if (item.id == selected) {
                auto kind = (item.actions & ActionBit(ActionKind::Equip)) ? ActionKind::Equip :
                    (item.actions & ActionBit(ActionKind::Use)) ? ActionKind::Use : ActionKind::Select;
                store.Queue({0, s.generation, s.revision, item.id, item.revision, 0, kind}, now);
                pressedAction = item.id; pressedActionUntil = now + 180;
            }
        }
    } else if (page == Page::Equipment && y >= 238 && y <= 962 && (inside(34, 312) || inside(928, 1206))) {
        auto groups = EquipmentGroups(s);
        size_t i = size_t((y - 238 + scroll) / 220) * 2 + (x >= 928 ? 1 : 0);
        if (i < groups.size()) {
            auto id = groups[i]->id; setPage(int(Page::Inventory)); selected = id;
            auto item = std::find_if(s.items.begin(), s.items.end(), [&](const Item& value) { return value.id == id; });
            if (item != s.items.end()) scroll = std::min(maximumScroll, float(size_t(item - s.items.begin()) / 5) * 164);
        }
    } else if (page == Page::Missions && x >= 338 && x <= 702 && y >= 238 && y <= 962) {
        size_t desired = size_t((y - 238 + scroll) / 120), row = 0;
        for (const auto& memory : s.memories) if ((memory.unlocked || memory.exposed) && row++ == desired) { selected = memory.id; break; }
    } else if (page == Page::Missions && x >= 748 && x <= 1182 && y >= 890 && y <= 950) {
        for (const auto& memory : s.memories) if (memory.id == selected || (!selected && memory.active)) {
            if (memory.unlocked && memory.replayable) {
                store.Queue({0, s.generation, s.revision, memory.id, memory.revision, 0, ActionKind::Replay}, now);
                pressedAction = memory.id; pressedActionUntil = now + 180;
            }
            break;
        }
    } else if (page == Page::Database && y >= 238 && y <= 962) {
        if (x < 280) {
            std::vector<std::wstring> categories;
            for (const auto& entry : s.database) if (entry.unlocked &&
                std::find(categories.begin(), categories.end(), entry.category) == categories.end()) categories.push_back(entry.category);
            if (y >= 254) { size_t i = size_t((y - 254 + categoryScroll) / 58); if (i < categories.size()) {
                category = categories[i]; scroll = 0; selected = 0; detailScroll = maximumDetailScroll = velocity = 0; Synchronize(s); } }
        } else if (x >= 302 && x <= 716) {
            size_t desired = size_t((y - 238 + scroll) / 116), row = 0;
            for (const auto& entry : s.database) if (entry.unlocked && (category.empty() || entry.category == category) && row++ == desired) {
                selected = entry.id; detailScroll = maximumDetailScroll = velocity = 0; break;
            }
        }
    }
}
}

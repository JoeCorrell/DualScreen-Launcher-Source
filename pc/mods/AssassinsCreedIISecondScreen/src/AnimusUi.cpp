#include "MissionNavigation.h"
#include "AnimusUi.h"
#include "MapRaster.h"
#include "MapLegend.h"
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
    auto r=MapBounds(s.map.get());return R(r.x,r.y,r.width,r.height);
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
          Color color = Ink, bool bold = false, bool center = false,bool singleLine=false) {
    FontFamily condensed(L"Arial Narrow");
    const FontFamily* family = condensed.GetLastStatus() == Ok ? &condensed : FontFamily::GenericSansSerif();
    Font font(family, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(color); StringFormat format;
    format.SetTrimming(StringTrimmingEllipsisWord);
    if(singleLine)format.SetFormatFlags(StringFormatFlagsNoWrap);
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
void Top(Graphics& g, const Snapshot& s, const UiState& ui, uint64_t) {
    // The antialiased gradient edges do not fully cover boundary pixels.
    // Reusing a DIB must produce the same frame as a fresh surface.
    g.Clear(Color(255, 253, 254, 251));
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
    Text(g,L"ANIMUS 2.0",R(34,20,300,46),31,Ink,true);
    Text(g,Tabs[int(ui.page)],R(356,23,560,46),30,Ink,true,true);
    Text(g,L"0"+std::to_wstring(int(ui.page)+1)+L" / 06",R(1030,27,170,35),22,Muted,false,true);
    Line(g,20,78,1220,78,Color(210,255,255,255),3);
    if(ui.page!=Page::Map){
        Text(g,L"GENETIC MEMORY ARCHIVE",R(38,108,1050,46),27,Muted);
        Line(g,34,160,1206,160);
        Text(g,s.location.empty()?Tabs[int(ui.page)]:s.location,R(38,178,1162,44),29,Ink,true);
    }
    Text(g, L"◈  ANIMUS", R(34, 1028, 230, 30), 21, Muted, true);
    Text(g, s.status, R(284, 1028, 920, 40), 21, Muted);
    Line(g, 32, 1011, 1208, 1011);
}
void DrawPlayer(Graphics& g,const Snapshot& s,const UiState& ui) {
    if (!(s.capabilities & Map) || !s.map) return;
    RectF area=MapArea(s); auto saved=g.Save(); g.SetClip(area);
    const auto mapX=ui.smoothPose?ui.displayPose.x:s.mapX,mapY=ui.smoothPose?ui.displayPose.y:s.mapY;
    const auto heading=ui.smoothPose?ui.displayPose.heading:s.heading;
    auto viewport=DisplayMapViewport(s,ui);
    auto point = [&](float x,float y) { return PointF(area.X+((x-viewport.x)*viewport.zoom+.5f)*area.Width,
        area.Y+((y-viewport.y)*viewport.zoom+.5f)*area.Height); };
        if (mapX && mapY && std::isfinite(*mapX) && std::isfinite(*mapY)) {
            auto center=point(*mapX,*mapY);
            auto icon=ui.images?ui.images->Request(PlayerMarkerArtwork):nullptr;
            if(!icon){g.Restore(saved);return;}
            if(heading && std::isfinite(*heading)){
            auto polar=[&](float angle,float length){return PointF(center.X+std::sin(angle)*length,center.Y-std::cos(angle)*length);};
            PointF cone[]{center,polar(*heading-.32f,76),polar(*heading+.32f,76)};
            SolidBrush glow(Color(75,155,21,30));g.FillPolygon(&glow,cone,3);
            }
            ImageAt(g,icon,R(center.X-27,center.Y-27,54,54));
        }
    g.Restore(saved);
}
void DrawMapMarkers(Graphics& g,const Snapshot& s,const UiState& ui,bool mapIcons=true) {
    if (!(s.capabilities & Map) || !s.map) return;
    const auto area=MapArea(s);auto saved=g.Save();g.SetClip(area);
    auto viewport=DisplayMapViewport(s,ui);
    for (const auto& marker:s.markers) {
        if(!MapMarkerVisible(marker,ui.mapFilter))continue;
        PointF p(area.X+((marker.x-viewport.x)*viewport.zoom+.5f)*area.Width,
                 area.Y+((marker.y-viewport.y)*viewport.zoom+.5f)*area.Height);
        if(!area.Contains(p))continue;
        if(marker.id==ui.selected){Pen selected(Red,3);g.DrawEllipse(&selected,p.X-23,p.Y-23,46.f,46.f);}
        if(!mapIcons)continue;
        auto icon=ui.images && marker.artworkId?ui.images->Request(marker.artworkId):nullptr;
        float size=MapMarkerSize(marker);
        if(icon)ImageAt(g,icon,R(p.X-size/2,p.Y-size/2,size,size));
        else{float radius=size*7/36;SolidBrush fill(marker.id==ui.selected?Red:Ink);g.FillEllipse(&fill,p.X-radius,p.Y-radius,radius*2,radius*2);}
    }
    DrawPlayer(g,s,ui);
    float northX=area.X+44,northY=area.Y+69;
    Text(g,L"N",R(northX-16,northY-52,32,30),23,Ink,true,true);
    PointF north[]{{northX,northY-20},{northX+12,northY},{northX,northY+26},{northX-12,northY}};
    SolidBrush compass(Color(190,70,74,70));Pen northOutline(Color(230,245,244,229),2);
    g.FillPolygon(&compass,north,4);g.DrawPolygon(&northOutline,north,4);g.Restore(saved);
}
void Diamond(Graphics& g,float x,float y,float radius,Color color,bool fill=true){
    PointF points[]{{x,y-radius},{x+radius,y},{x,y+radius},{x-radius,y}};
    if(fill){SolidBrush brush(color);g.FillPolygon(&brush,points,4);}
    Pen pen(color,1.5f);g.DrawPolygon(&pen,points,4);
}
void DrawMap(Graphics& g,const Snapshot& s,UiState& ui,bool motion,bool raster){
    Fill(g,R(18,96,346,906),Color(188,245,248,249));
    Line(g,370,96,370,1002,Color(200,167,181,186),3);
    Fill(g,R(382,96,838,906),Color(255,220,221,213));RectF area=MapArea(s);
    if(!(s.capabilities&Map) || !s.map)Unavailable(g,area,L"CITY MAP");
    else{
        auto saved=g.Save();g.SetClip(area);auto viewport=DisplayMapViewport(s,ui);const auto& map=s.map;
        if(raster && map->width>0 && map->height>0 && map->width<=4096 && map->height<=4096 &&
            map->bgra.size()==size_t(map->width)*map->height){
            Bitmap bitmap(map->width,map->height,map->width*4,PixelFormat32bppARGB,
                reinterpret_cast<BYTE*>(const_cast<uint32_t*>(map->bgra.data())));
            g.DrawImage(&bitmap,area,(viewport.x-.5f/viewport.zoom)*map->width,
                (viewport.y-.5f/viewport.zoom)*map->height,map->width/viewport.zoom,map->height/viewport.zoom,UnitPixel);
        }
        if(motion)DrawMapMarkers(g,s,ui);g.Restore(saved);
    }
    Diamond(g,48,139,17,Color(255,164,156,134));
    Font city(FontFamily::GenericSerif(),34,FontStyleRegular,UnitPixel);SolidBrush cityInk(Ink);
    StringFormat cityFormat;cityFormat.SetTrimming(StringTrimmingEllipsisCharacter);
    std::wstring location=s.location.empty()?L"CITY MAP":s.location;
    g.DrawString(location.c_str(),INT(location.size()),&city,R(78,111,272,58),&cityFormat,&cityInk);
    Line(g,34,176,350,176);
    auto legend=MapLegend(s);size_t total=0,shown=0;
    for(const auto& row:legend){total+=row.count;if(!ui.mapFilter || ui.mapFilter==row.id)shown+=row.count;}
    bool player=s.mapX && s.mapY && (s.capabilities&Map);
    Text(g,L"ALL MARKERS",R(34,191,160,32),20,ui.mapFilter?Muted:Ink);
    Text(g,s.mapMarkersVerified?std::to_wstring(shown+(player?1:0))+L" / "+std::to_wstring(total+(player?1:0)):L"\u2014",R(226,191,124,32),20,Ink,false,true);
    if(ui.following && !ui.selected)Fill(g,R(32,232,314,46),Red);
    if(ui.images)ImageAt(g,ui.images->Request(PlayerMarkerArtwork),R(36,238,34,34));
    Text(g,L"Ezio",R(80,241,186,32),24,ui.following && !ui.selected?Color::White:Ink);
    Text(g,player?L"1":L"\u2014",R(303,241,38,32),22,ui.following && !ui.selected?Color::White:Ink,false,true);
    auto saved=g.Save();g.SetClip(R(30,286,320,402));
    for(size_t i=0;i<legend.size();++i){const auto& row=legend[i];float y=286.f+float(i)*48-ui.categoryScroll;
        if(y+48<286 || y>688)continue;bool selected=ui.mapFilter==row.id;
        if(selected)Fill(g,R(32,y,314,46),Color(235,137,24,31));
        auto icon=ui.images && row.artwork?ui.images->Request(row.artwork):nullptr;
        if(icon)ImageAt(g,icon,R(36,y+5,34,34));else Diamond(g,53,y+23,10,selected?Color::White:Ink,false);
        Text(g,row.name,R(80,y+10,221,32),20,selected?Color::White:Ink,false,false,true);
        Text(g,s.mapMarkersVerified?std::to_wstring(row.count):L"\u2014",R(304,y+10,36,32),21,selected?Color::White:Ink,false,true);
        Line(g,78,y+47,345,y+47,Color(100,165,179,184));
    }
    g.Restore(saved);
    if(ui.maximumCategoryScroll>0){
        Fill(g,R(355,286,3,402),Color(255,209,218,221));
        float thumb=std::max(38.f,402*402/(402+ui.maximumCategoryScroll));
        Fill(g,R(353,286+(402-thumb)*ui.categoryScroll/ui.maximumCategoryScroll,6,thumb),Color(255,140,152,158));
    }
    Line(g,30,714,352,714,Color(255,255,255,255),3);
    const Marker* selected=nullptr;for(const auto& marker:s.markers)if(marker.id==ui.selected && MapMarkerVisible(marker,ui.mapFilter))selected=&marker;
    if(selected){
        Text(g,L"MARKER DETAILS",R(34,738,312,38),25,Muted);
        Text(g,selected->name,R(34,792,310,64),27,Red,true);
        Text(g,selected->description,R(34,858,312,134),22,Ink);
    }else{
        Diamond(g,49,759,14,Muted);Text(g,L"PLAYER STATUS",R(77,744,273,38),25,Ink);
        Line(g,34,792,350,792);Text(g,L"Health",R(34,810,100,34),24);
        Text(g,Number(s.health)+L" / "+Number(s.maximumHealth),R(252,810,96,30),21,Muted,false,true,true);
        for(int i=0;i<10;++i){float fraction=s.health && s.maximumHealth && *s.maximumHealth>0?float(*s.health / *s.maximumHealth):0;
            Diamond(g,45.f+i*32,854,11,i<10*fraction?Red:Color(255,163,174,180),i<10*fraction);}
        Text(g,L"Notoriety",R(34,892,132,30),23);
        Bar(g,R(170,901,90,12),s.notoriety,100.0);
        Text(g,Number(s.notoriety)+(s.notoriety?L"%":L""),R(264,892,85,32),21,Muted,false,true,true);
        Text(g,L"Florins",R(34,949,107,36),24);Text(g,Number(s.money)+L"  f",R(147,946,200,45),30,Ink,false,true,true);
    }
}
std::shared_ptr<const Image> Artwork(const UiState& ui, uint32_t id, const std::shared_ptr<const Image>& fallback) {
    return fallback ? fallback : ui.images ? ui.images->Request(id) : nullptr;
}
void ItemCard(Graphics& g, const Item& item, RectF r, bool selected, const UiState& ui) {
    Box(g, r, selected); ImageAt(g, Artwork(ui, item.thumbnailId ? item.thumbnailId : item.artworkId, item.icon),
        R(r.X + 12, r.Y + 8, r.Width - 24, r.Height - 70));
    Text(g, item.name, R(r.X + 9, r.GetBottom() - 59, r.Width - 18, 54), 21);
    if (item.quantity) Text(g, std::to_wstring(*item.quantity), R(r.GetRight() - 57, r.Y + 7, 46, 32), 23, Ink, true, true);
    if (item.equipped) { Fill(g, R(r.X + 2, r.GetBottom() - 4, r.Width - 4, 3), Red); }
}
void DrawInventory(Graphics& g, const Snapshot& s, UiState& ui, bool equipment, uint64_t now) {
    if (equipment) {
        // Symmetric 19px gutters; all three panels share their top/bottom edges.
        Box(g, R(34,238,278,724));Box(g,R(928,238,278,724));
        Box(g, R(331, 238, 578, 724));
        if (s.preview) ImageAt(g, s.preview, R(342, 249, 556, 702));
        else Unavailable(g, R(331, 238, 578, 724), L"EZIO AUDITORE");
        auto groups = EquipmentGroups(s); auto saved = g.Save(); g.SetClip(R(34, 249, 1172, 702));
        for (size_t i = 0; i < groups.size(); ++i) {
            float x = i % 2 ? 939.f : 45.f, y = 249.f + float(i / 2) * 234 - ui.scroll;
            if (y + 222 < 249 || y > 951) continue;
            Text(g, groups[i]->category, R(x, y, 256, 35), 24, Ink, true);
            ItemCard(g, *groups[i], R(x, y + 38, 256, 184), ui.selected == groups[i]->id, ui);
        }
        g.Restore(saved);
        return;
    }
    RectF grid = R(34, 238, 786, 724); auto saved = g.Save(); g.SetClip(grid);
    for (size_t i = 0; i < s.items.size(); ++i) {
        RectF slot = R(34.f + float(i % 5) * 157, 238.f + float(i / 5) * 164 - ui.scroll, 145, 152);
        if (slot.GetBottom() >= grid.Y && slot.Y < grid.GetBottom()) ItemCard(g, s.items[i], slot, ui.selected == s.items[i].id, ui);
    }
    g.Restore(saved);
    if (!(s.capabilities & Inventory)) Unavailable(g, grid, L"POSSESSIONS");
    else if (s.items.empty()) Text(g, L"NO POSSESSIONS AVAILABLE", R(104, 548, 646, 70), 28, Muted, false, true);
    Box(g, R(842, 238, 364, 724));
    const Item* item = nullptr; for (const auto& candidate : s.items) if (candidate.id == ui.selected) item = &candidate;
    if (!item) { Text(g, L"Select an item to inspect", R(866, 551, 316, 70), 26, Muted, false, true); return; }
    ImageAt(g, Artwork(ui, item->artworkId, item->icon), R(870, 258, 308, 230));
    Text(g, item->name, R(866, 508, 316, 78), 30, Ink, true);
    float descriptionY=592;
    if(item->weaponRatings){
        constexpr const wchar_t* names[]{L"DAMAGE",L"SPEED",L"DEFLECTION"};
        for(unsigned row=0;row<3;++row){
            float y=592.f+row*54;auto value=(*item->weaponRatings)[row];
            Text(g,names[row],R(866,y,230,27),20,Muted,true);
            Text(g,std::to_wstring(value)+L" / 5",R(1100,y,80,27),20,Ink,true);
            for(unsigned segment=0;segment<5;++segment)
                Fill(g,R(866.f+segment*62,y+30,56,8),segment<value?Red:Color(255,214,218,218));
        }
        descriptionY=762;
    }
    ScrollingText(g, item->description + L"\n\n" + item->details, R(866, descriptionY, 306, 862-descriptionY), ui);
    if (item->actions && item->owned) { Box(g, R(866, 878, 316, 60), true);
        bool pressed = ui.pressedAction == item->id && now < ui.pressedActionUntil;
        if (pressed) Fill(g, R(867, 879, 314, 58), Red);
        Text(g, (item->actions & ActionBit(ActionKind::Equip)) ? L"EQUIP" :
            (item->actions & ActionBit(ActionKind::Use)) ? L"USE" : L"SELECT", R(866, 891, 316, 40), 25,
            pressed ? Color::White : Red, true, true); }
    else if (item->quantity && item->capacity)
        Text(g, std::to_wstring(*item->quantity) + L" / " + std::to_wstring(*item->capacity),
            R(866, 891, 316, 40), 25, Red, true, true);
}
void DrawMissions(Graphics& g, const Snapshot& s, UiState& ui, uint64_t now,bool motion) {
    Box(g,R(34,238,282,724));
    Text(g,L"DNA SEQUENCES",R(52,254,246,42),25,Ink,true,true);
    Box(g,R(52,304,246,52),true);
    bool currentPressed=ui.pressedAction==UINT64_MAX && now<ui.pressedActionUntil;
    if(currentPressed)Fill(g,R(53,305,244,50),Red);
    Text(g,L"CURRENT MEMORY",R(52,316,246,32),22,currentPressed?Color::White:Red,true,true);
    auto sequences=MissionSequences(s);
    auto saved=g.Save();g.SetClip(R(46,374,258,398));
    const MissionSequence* sequence=nullptr;
    for(size_t i=0;i<sequences.size();++i){
        const auto& entry=sequences[i];bool chosen=entry.id==ui.selectedSequence;
        if(chosen)sequence=&entry;
        auto r=R(46,374.f+float(i)*92-ui.categoryScroll,258,82);
        if(r.GetBottom()<374 || r.Y>772)continue;
        Box(g,r,chosen);
        Text(g,entry.title.empty()?L"GENETIC MEMORY":entry.title,R(r.X+12,r.Y+7,234,44),23,chosen?Red:Ink,true);
        Text(g,entry.active?L"ACTIVE SEQUENCE":std::to_wstring(entry.completed)+L" COMPLETE / "+std::to_wstring(entry.visible)+L" VISIBLE",
            R(r.X+12,r.Y+56,234,22),16,Muted,true);
    }
    g.Restore(saved);
    if(sequence)Text(g,std::to_wstring(sequence->visible)+L" VISIBLE MEMORIES",R(52,790,246,30),20,Muted,true,true);
    if(motion)Dna(g,R(116,838,116,102),now*.00018);
    auto memories=SequenceMemories(s,ui.selectedSequence);
    saved=g.Save();g.SetClip(R(338,238,364,724));
    const Memory* selected=nullptr;
    for(size_t i=0;i<memories.size();++i){
        const auto& memory=*memories[i];if(memory.id==ui.selected)selected=&memory;
        auto r=R(338,238.f+float(i)*120-ui.scroll,364,106);
        if(r.GetBottom()<238 || r.Y>962)continue;
        Box(g,r,memory.id==ui.selected);
        Text(g,!memory.unlocked?L"LOCKED":memory.active?L"ACTIVE MEMORY":memory.complete?L"COMPLETED":L"AVAILABLE",
            R(r.X+16,r.Y+10,332,26),18,memory.active?Red:Muted,true);
        Text(g,memory.unlocked?memory.name:L"MEMORY LOCKED",R(r.X+16,r.Y+40,332,55),26,memory.unlocked?Ink:Muted,true);
    }
    g.Restore(saved);Box(g,R(724,238,482,724));
    if(!(s.capabilities&Missions)){Unavailable(g,R(724,238,482,724),L"GENETIC MEMORY");return;}
    if(!selected){Text(g,L"NO DISCOVERED MEMORIES",R(748,500,434,90),28,Muted,true,true);return;}
    if(!selected->unlocked){Text(g,L"MEMORY LOCKED",R(748,550,434,70),30,Muted,true,true);return;}
    Text(g,selected->name,R(748,266,434,92),32,Ink,true);
    std::wstring description;
    if(!selected->objective.empty())description=L"CURRENT OBJECTIVE\n"+selected->objective;
    else description=selected->complete?L"MEMORY COMPLETED":selected->active?L"ACTIVE MEMORY":L"MEMORY AVAILABLE";
    if(!selected->description.empty())description+=L"\n\n"+selected->description;
    ScrollingText(g,description,R(748,372,424,414),ui);
    Text(g,L"SYNCHRONIZATION  "+Number(selected->synchronization,true),R(748,812,434,42),23,Ink,true);
    Bar(g,R(748,862,434,12),selected->synchronization,100);
    if(selected->replayable){
        Box(g,R(748,890,434,60),true);
        bool pressed=ui.pressedAction==selected->id && now<ui.pressedActionUntil;
        if(pressed)Fill(g,R(749,891,432,58),Red);
        Text(g,L"REPLAY MEMORY",R(748,906,434,36),25,pressed?Color::White:Red,true,true);
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
        Box(g, r, entry.id == ui.selected); ImageAt(g, Artwork(ui, entry.artworkId, entry.illustration), R(308, r.Y + 6, 104, 92));
        Text(g, entry.title, R(424, r.Y + 18, 274, 80), 26, Ink, true);
    }
    g.Restore(saved); Box(g, R(738, 238, 468, 724));
    if (!(s.capabilities & Database)) Unavailable(g, R(738, 238, 468, 724), L"HISTORICAL ARCHIVE");
    else if (selected) { ImageAt(g, Artwork(ui, selected->artworkId, selected->illustration), R(758, 258, 428, 252));
        Text(g, selected->title, R(762, 530, 420, 82), 30, Ink, true);
        ScrollingText(g, selected->text, R(762, 626, 410, 312), ui); }
    else Text(g, L"NO ENTRIES UNLOCKED", R(762, 548, 420, 80), 26, Muted, false, true);
}
void DrawProgress(Graphics& g, const Snapshot& s, const UiState& ui, uint64_t now,bool motion) {
    Text(g, L"OVERALL SYNCHRONIZATION  " + Number(s.synchronization, true), R(52, 260, 1136, 58), 35, Ink, true);
    Bar(g, R(52, 338, 1136, 18), s.synchronization, 100);
    if(motion) Dna(g, R(526, 418, 188, 516), now * .00018);
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

MapViewport DisplayMapViewport(const Snapshot& s,const UiState& ui) {
    auto viewport=ui.map;
    const auto x=ui.smoothPose?ui.displayPose.x:s.mapX,y=ui.smoothPose?ui.displayPose.y:s.mapY;
    if(ui.following && x && y) { viewport.x=*x; viewport.y=*y; }
    viewport.Clamp(); return viewport;
}
void DrawAnimusMotion(Graphics& g,const Snapshot& s,const UiState& ui,uint64_t now,bool mapIcons) {
    g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    if(ui.page==Page::Map) DrawMapMarkers(g,s,ui,mapIcons);
    else if(ui.page==Page::Missions) Dna(g,R(116,838,116,102),now*.00018);
    else if(ui.page==Page::Progress) Dna(g,R(526,418,188,516),now*.00018);
}
void DrawAnimus(Graphics& g, const Snapshot& s, UiState& ui, uint64_t now,bool motion,bool mapRaster) {
    g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    Top(g, s, ui, now);
    switch (ui.page) {
        case Page::Map: DrawMap(g, s, ui,motion,mapRaster); break;
        case Page::Equipment: DrawInventory(g, s, ui, true, now); break;
        case Page::Inventory: DrawInventory(g, s, ui, false, now); break;
        case Page::Missions: DrawMissions(g, s, ui, now,motion); break;
        case Page::Database: DrawDatabase(g, s, ui); break;
        case Page::Progress: DrawProgress(g, s, ui, now,motion); break;
    }
}
void UiState::Synchronize(const Snapshot& s) {
    if (s.generation != generation) {
        generation = s.generation; selected = 0; selectedSequence = 0; scroll = 0; velocity = 0;
        category.clear(); map = {}; dragging = false; following = true;
        mapFilter=0;swipe.Reset();pageTransition=0;pageDirection=0;
        detailScroll = maximumDetailScroll = categoryScroll = maximumCategoryScroll = 0;
        pressedAction = pressedActionUntil = 0;
    }
    size_t rows = 0;
    float height = 0;
    if(page==Page::Map){
        auto legend=MapLegend(s);maximumCategoryScroll=std::max(0.f,float(legend.size())*48-402);
        categoryScroll=std::clamp(categoryScroll,0.f,maximumCategoryScroll);
        if(mapFilter && std::none_of(legend.begin(),legend.end(),[&](const MapLegendRow& row){return row.id==mapFilter;}))mapFilter=0;
        if(selected && std::none_of(s.markers.begin(),s.markers.end(),[&](const Marker& marker){return marker.id==selected && MapMarkerVisible(marker,mapFilter);}))selected=0;
    }
    if (page == Page::Inventory) { rows = (s.items.size() + 4) / 5; height = 164; }
    if (page == Page::Inventory && selected && std::none_of(s.items.begin(),s.items.end(),
        [&](const Item& item){return item.id==selected;})) {
        selected=0;detailScroll=maximumDetailScroll=velocity=0;pressedAction=pressedActionUntil=0;
    }
    if (page == Page::Equipment) { rows = (EquipmentGroups(s).size() + 1) / 2; height = 234; }
    if(page==Page::Missions){
        auto sequences=MissionSequences(s);
        if(std::none_of(sequences.begin(),sequences.end(),[&](const MissionSequence& value){return value.id==selectedSequence;})){
            auto active=std::find_if(sequences.begin(),sequences.end(),[](const MissionSequence& value){return value.active;});
            selectedSequence=active!=sequences.end()?active->id:sequences.empty()?0:sequences.front().id;
            selected=0;scroll=detailScroll=maximumDetailScroll=velocity=0;
            for(size_t i=0;i<sequences.size();++i)if(sequences[i].id==selectedSequence)categoryScroll=float(i)*92;
        }
        auto memories=SequenceMemories(s,selectedSequence);
        if(std::none_of(memories.begin(),memories.end(),[&](const Memory* value){return value->id==selected;})){
            auto active=std::find_if(memories.begin(),memories.end(),[](const Memory* value){return value->active && value->unlocked;});
            selected=active!=memories.end()?(*active)->id:memories.empty()?0:memories.front()->id;
            detailScroll=maximumDetailScroll=0;
            for(size_t i=0;i<memories.size();++i)if(memories[i]->id==selected)scroll=float(i)*120;
        }
        rows=memories.size();height=120;
        maximumCategoryScroll=std::max(0.f,float(sequences.size())*92-398);
        categoryScroll=std::clamp(categoryScroll,0.f,maximumCategoryScroll);
    }
    if (page == Page::Database) {
        std::vector<std::wstring> categories;
        for(const auto& entry:s.database) if(entry.unlocked &&
            std::find(categories.begin(),categories.end(),entry.category)==categories.end()) categories.push_back(entry.category);
        if(std::find(categories.begin(),categories.end(),category)==categories.end()) {
            category=categories.empty()?L"":categories.front();
            selected=0;scroll=categoryScroll=detailScroll=maximumDetailScroll=velocity=0;
        }
        const DatabaseEntry* first=nullptr;bool valid=false;
        for(const auto& entry:s.database) if(entry.unlocked && entry.category==category) {
            if(!first)first=&entry;
            valid|=entry.id==selected;++rows;
        }
        if(!valid){selected=first?first->id:0;scroll=detailScroll=maximumDetailScroll=velocity=0;}
        maximumCategoryScroll=std::max(0.f,float(categories.size())*58-692);
        categoryScroll=std::clamp(categoryScroll,0.f,maximumCategoryScroll);
        height=116;
    }
    maximumScroll = std::max(0.f, float(rows) * height - (page==Page::Equipment?702:724));
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
    if (pressedAction && now >= pressedActionUntil) { pressedAction = 0; changed = true; }
    return changed;
}
void UiState::TouchInput(const Touch& t, const Snapshot& s, StateStore& store, uint64_t now) {
    Synchronize(s); float x = t.x * Width, y = t.y * Height;
    auto setPage = [&](int p) { pageDirection=p>int(page)?1:-1;pageTransition=now;page = Page((p + 6) % 6); scroll = 0; selected = 0; selectedSequence = 0; velocity = 0;
        detailScroll = maximumDetailScroll = categoryScroll = maximumCategoryScroll = 0;
        category.clear(); dragging = false; Synchronize(s); };
    if (t.kind == TouchKind::Next || t.kind == TouchKind::Previous) { setPage(int(page) + (t.kind == TouchKind::Next ? 1 : -1)); return; }
    if(t.kind==TouchKind::Cancel){swipe.Cancel(now);dragging=false;velocity=0;pressedAction=0;return;}
    const bool selection = t.kind == TouchKind::Tap || t.kind == TouchKind::Hold;
    if(t.kind==TouchKind::Down)swipe.Down(t.x,t.y,page==Page::Map,now);
    if(t.kind==TouchKind::Move && swipe.Move(t.x,t.y)){dragging=false;velocity=0;pressedAction=0;return;}
    if(t.kind==TouchKind::Up){int direction=swipe.Up(t.x,t.y,now);if(direction){setPage(int(page)+direction);return;}}
    if(t.kind==TouchKind::Pinch)swipe.Cancel(now);
    if(selection && swipe.SuppressTap(now))return;
    auto inside = [&](float left, float right) { return x >= left && x <= right && y >= 238 && y <= 962; };
    auto mapArea = MapArea(s);
    bool inMap = mapArea.Contains(x, y);
    auto captureFollow = [&] {
        if (following) map=DisplayMapViewport(s,*this);
        following = false;
    };
    if (t.kind == TouchKind::Down) {
        if(page==Page::Missions && x>=52 && x<=298 && y>=304 && y<=356){
            pressedAction=UINT64_MAX;pressedActionUntil=now+180;dragging=false;velocity=0;return;
        }
        dragRegion = page == Page::Map && inMap ? 1 :
            ((page == Page::Inventory && inside(34, 820)) || (page == Page::Missions && inside(338, 702)) ||
             (page == Page::Database && inside(302, 716)) || (page == Page::Progress && y >= 410 && y <= 954)) ? 2 :
             (page == Page::Equipment && (inside(34, 312) || inside(928, 1206))) ? 2 :
             ((page == Page::Database && inside(738, 1206) && y >= 626) ||
              (page == Page::Inventory && inside(842, 1206) && y >= 592 && y <= 862) ||
              (page == Page::Missions && inside(724,1206) && y>=372 && y<=786)) ? 3 :
             ((page==Page::Map && x>=30 && x<=364 && y>=286 && y<=688) ||
              (page == Page::Database && inside(34, 280)) ||
              (page == Page::Missions && inside(34,316) && y>=374 && y<=772)) ? 4 : 0;
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
    if (!selection) return;
    if (page == Page::Map) {
        if(x>=30 && x<=364 && y>=186 && y<=230){mapFilter=0;selected=0;return;}
        if(x>=30 && x<=364 && y>=232 && y<=278){following=true;selected=0;return;}
        if(x>=30 && x<=350 && y>=286 && y<=688){
            auto legend=MapLegend(s);size_t row=size_t((y-286+categoryScroll)/48);
            if(row<legend.size()){mapFilter=mapFilter==legend[row].id?0:legend[row].id;selected=0;}
            return;
        }
        if (!inMap) return;
        MapViewport view=DisplayMapViewport(s,*this);
        float nearest = 28; selected = 0;
        for (const auto& marker : s.markers) { if(!MapMarkerVisible(marker,mapFilter))continue;
            float mx = mapArea.X + ((marker.x - view.x) * view.zoom + .5f) * mapArea.Width;
            float my = mapArea.Y + ((marker.y - view.y) * view.zoom + .5f) * mapArea.Height;
            float distance = std::hypot(x - mx, y - my); if (distance < nearest) { nearest = distance; selected = marker.id; } }
    } else if (page == Page::Inventory) {
        if (x >= 34 && x < 819 && y >= 238 && y <= 962) {
            size_t i = size_t((y - 238 + scroll) / 164) * 5 + size_t((x - 34) / 157);
            if (i < s.items.size()) { selected = s.items[i].id; detailScroll = maximumDetailScroll = velocity = 0; }
        } else if (t.kind == TouchKind::Tap && x >= 866 && x <= 1182 && y >= 878 && y <= 938) {
            for (const auto& item : s.items) if (item.id == selected && item.owned &&
                (item.actions & (ActionBit(ActionKind::Equip)|ActionBit(ActionKind::Use)|ActionBit(ActionKind::Select)))) {
                auto kind = (item.actions & ActionBit(ActionKind::Equip)) ? ActionKind::Equip :
                    (item.actions & ActionBit(ActionKind::Use)) ? ActionKind::Use : ActionKind::Select;
                if(store.Queue({0, s.generation, s.revision, item.id, item.revision, 0, kind}, now)) {
                    pressedAction = item.id; pressedActionUntil = now + 180;
                }
            }
        }
    } else if (page == Page::Equipment && y >= 249 && y <= 951 && (inside(34, 312) || inside(928, 1206))) {
        auto groups = EquipmentGroups(s);
        size_t i = size_t((y - 249 + scroll) / 234) * 2 + (x >= 928 ? 1 : 0);
        if (i < groups.size()) {
            auto id = groups[i]->id; setPage(int(Page::Inventory)); selected = id;
            auto item = std::find_if(s.items.begin(), s.items.end(), [&](const Item& value) { return value.id == id; });
            if (item != s.items.end()) scroll = std::min(maximumScroll, float(size_t(item - s.items.begin()) / 5) * 164);
        }
    } else if(page==Page::Missions && x>=34 && x<=316 && y>=304 && y<=356){
        pressedAction=UINT64_MAX;pressedActionUntil=now+180;
        selectedSequence=selected=0;scroll=categoryScroll=detailScroll=velocity=0;Synchronize(s);
        auto memories=SequenceMemories(s,selectedSequence);
        for(size_t i=0;i<memories.size();++i)if(memories[i]->id==selected)scroll=std::min(maximumScroll,float(i)*120);
        auto sequences=MissionSequences(s);
        for(size_t i=0;i<sequences.size();++i)if(sequences[i].id==selectedSequence)categoryScroll=std::min(maximumCategoryScroll,float(i)*92);
    } else if(page==Page::Missions && x>=34 && x<=316 && y>=374 && y<=772){
        auto sequences=MissionSequences(s);size_t i=size_t((y-374+categoryScroll)/92);
        if(i<sequences.size()){
            selectedSequence=sequences[i].id;selected=0;scroll=detailScroll=maximumDetailScroll=velocity=0;Synchronize(s);
        }
    } else if (page == Page::Missions && x >= 338 && x <= 702 && y >= 238 && y <= 962) {
        auto memories=SequenceMemories(s,selectedSequence);size_t i=size_t((y-238+scroll)/120);
        if(i<memories.size()){selected=memories[i]->id;detailScroll=maximumDetailScroll=velocity=0;}
    } else if (t.kind == TouchKind::Tap && page == Page::Missions && x >= 748 && x <= 1182 && y >= 890 && y <= 950) {
        for (const auto& memory : s.memories) if (memory.id == selected || (!selected && memory.active)) {
            if (memory.unlocked && memory.replayable) {
                store.Queue({0, s.generation, s.revision, memory.id, memory.revision, 0, ActionKind::Replay}, now);
                pressedAction = memory.id; pressedActionUntil = now + 180;
            }
            break;
        }
    } else if (page == Page::Database && y >= 238 && y <= 962) {
        if (x >= 46 && x < 268 && y <= 946) {
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

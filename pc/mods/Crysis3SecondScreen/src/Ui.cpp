#include "Ui.h"
#include <cstdio>

namespace c3 {
namespace {
constexpr dis::Color Cyan{133,235,234}, Muted{116,157,159}, Border{43,91,94}, Yellow{242,200,86};
dis::Box Box(Rect r) { return {int(r.x),int(r.y),int(r.w),int(r.h)}; }
std::string NumberText(Number value) {
    if (!value.known || !std::isfinite(value.value)) return "--";
    char result[48]{}; std::snprintf(result,sizeof(result),"%.1f",double(value.value)); return result;
}
std::string Count(Integer value) { return value.known ? std::to_string(value.value) : "--"; }
const char* State(Value<bool> value) { return !value.known ? "UNAVAILABLE" : value.value ? "ACTIVE" : "INACTIVE"; }
constexpr const char* Names[]{"TACTICAL MAP","WEAPONS / LOADOUT","NANOSUIT / UPGRADES","OBJECTIVES / MISSION INTEL","PLAYER STATUS / STATISTICS"};
static_assert(sizeof(Names)/sizeof(Names[0])==size_t(Page::Count));
}
void Ui::Label(Rect box,const std::string& text,int size,dis::Color color) {
    canvas_.Text(Box(box),text,{size,FW_NORMAL,false},color);
}
void Ui::Panel(Rect box,const std::string& title) {
    canvas_.FillRect(Box(box),{7,17,20});
    canvas_.StrokeRect(Box(box),1,Border);
    canvas_.HGradient({int(box.x+1),int(box.y+1),int(box.w-2),44},{19,46,48},{9,23,26});
    Label({box.x+18,box.y+4,box.w-36,36},title,20,Cyan);
    canvas_.Line(int(box.x),int(box.y),int(box.x+30),int(box.y),3,Cyan);
    canvas_.Line(int(box.x),int(box.y),int(box.x),int(box.y+22),3,Cyan);
}
void Ui::Empty(Rect box,const std::string& message) {
    canvas_.SetClip(Box(box));
    canvas_.TextWrap(Box({box.x+28,box.y+box.h/2-30,box.w-56,120}),message,{22,FW_NORMAL,false},Muted,dis::Align::kCenter);
    canvas_.ClearClip();
}
void Ui::Button(Rect box,const std::string& label,const Capability& capability,const Snapshot& s,uint64_t now) {
    bool enabled=CanAct(s,capability,now);
    canvas_.FillRect(Box(box),enabled?dis::Color{18,54,57}:dis::Color{14,24,28});
    canvas_.StrokeRect(Box(box),1,enabled?Cyan:Border);
    Label({box.x+14,box.y,box.w-28,box.h},label,20,enabled?Cyan:Muted);
    if (enabled && targetCount_<targets_.size()) targets_[targetCount_++]={box,capability};
}
void Ui::Meter(Rect box,const std::string& label,Number value,Number maximum) {
    Label({box.x,box.y,box.w-140,32},label,20,Cyan);
    Label({box.x+box.w-130,box.y,130,32},NumberText(value),24);
    const dis::Box track{int(box.x),int(box.y+46),int(box.w),14};
    canvas_.SetClip(track);
    canvas_.Bar(track,Ratio(value,maximum),Cyan,{28,48,51});
    canvas_.ClearClip();
}
const Ui::Target* Ui::Hit(float x,float y) const {
    for (size_t i=0;i<targetCount_;++i) if (targets_[i].bounds.Contains(x,y)) return &targets_[i];
    return nullptr;
}
void Ui::PointerDown(float x,float y,const Snapshot& s,uint64_t now) {
    pressed_={}; navigation.Down(x,y);
    if (renderedGeneration_!=s.generation || renderedSequence_!=s.sequence ||
        renderedPage_!=navigation.page || renderedScroll_!=navigation.scroll[int(navigation.page)]) return;
    const auto* target=Hit(x,y);
    if (target && CanAct(s,target->capability,now) && HasCapability(s,target->capability)) {
        pressed_=target->capability; pressedGeneration_=s.generation;
        pressedPage_=navigation.page; pressedScroll_=navigation.scroll[int(navigation.page)];
    }
}
std::optional<Command> Ui::PointerUp(float x,float y,const Snapshot& s,uint64_t now) {
    const auto owned=pressed_; pressed_={};
    if (!navigation.Up(x,y) || !owned.key || pressedGeneration_!=s.generation ||
        pressedPage_!=navigation.page || pressedScroll_!=navigation.scroll[int(navigation.page)]) return {};
    const auto* target=Hit(x,y);
    if (!target || target->capability.key!=owned.key || target->capability.action!=owned.action) return {};
    return Tap(x,y,s,now);
}
std::optional<Command> Ui::Tap(float x,float y,const Snapshot& s,uint64_t now) {
    if (renderedGeneration_!=s.generation || renderedSequence_!=s.sequence || renderedPage_!=navigation.page ||
        renderedScroll_!=navigation.scroll[int(navigation.page)] ||
        (lastActionAt_ && (now<lastActionAt_ || now-lastActionAt_<150))) return {};
    for (size_t i=0;i<targetCount_;++i) {
        const auto& target=targets_[i];
        if (target.bounds.Contains(x,y) && CanAct(s,target.capability,now) && HasCapability(s,target.capability)) {
            lastActionAt_=now;
            return Command{nextRequest_++,s.generation,s.sequence,now,target.capability};
        }
    }
    return {};
}
void Ui::Render(const Snapshot& snapshot,uint64_t now) {
    navigation.Synchronize(snapshot,now);
    // One expired field invalidates the whole published frame. Never retain
    // last mission's numbers while showing a new lifecycle message.
    Snapshot unavailable{};
    const bool live=Current(snapshot,now);
    const Snapshot& s=live?snapshot:unavailable;
    const Artwork* mapArt=artwork.Map(s.map.artwork);
    navigation.mapReady=navigation.mapReady && mapArt;
    if (mapArt) navigation.map.viewport=mapArt->Fit(MapArea);
    targetCount_=0;
    renderedGeneration_=snapshot.generation; renderedSequence_=snapshot.sequence; renderedPage_=navigation.page;
    const int page=int(navigation.page);
    canvas_.Clear({4,11,14});
    // Sparse, code-native technical motif; no invented mission imagery.
    for (int y=16;y<1080;y+=96) for (int x=1030;x<1240;x+=84) {
        const int hx=x+((y/96)%2)*42;
        const int px[]{hx-24,hx+24,hx+42,hx+24,hx-24,hx-42,hx-24};
        const int py[]{y-38,y-38,y,y+38,y+38,y,y-38};
        for (int i=0;i<6;++i) canvas_.Line(px[i],py[i],px[i+1],py[i+1],1,{15,39,43});
    }
    // Compact page identity only; the display is devoted to game information.
    canvas_.FillRect({24,22,58,36},Cyan);
    Label({32,21,48,38},"0"+std::to_string(page+1),25,{5,22,24});
    Label({102,14,1090,50},Names[page],29);
    canvas_.Line(24,70,1216,70,1,Border);
    size_t rows=0,visibleRows=1;
    switch(navigation.page) {
        case Page::Weapons: rows=WeaponPanelCount(s.weapons); visibleRows=2; break;
        case Page::Suit: rows=s.modules.size(); visibleRows=VisibleModules; break;
        case Page::Objectives: rows=s.objectives.size(); visibleRows=VisibleObjectives; break;
        case Page::Status: rows=s.statistics.size(); visibleRows=VisibleStatistics; break;
        default: break;
    }
    navigation.scroll[page]=std::clamp(navigation.scroll[page],0,int(rows>visibleRows?rows-visibleRows:0));
    const int scroll=navigation.scroll[page]; renderedScroll_=scroll;
    if (navigation.page==Page::Map) {
        Panel(Content,"TACTICAL OVERVIEW");
        if (!navigation.mapReady) Empty(MapArea,"MAP DATA UNAVAILABLE\nA verified level map and player projection are required.");
        else {
            const auto& view=navigation.map;
            const auto& bounds=view.viewport;
            const float half=0.5f/view.zoom;
            dis::Box crop{int((view.centerX-half)*mapArt->width),int((view.centerY-half)*mapArt->height),
                          std::max(1,int(mapArt->width/view.zoom)),std::max(1,int(mapArt->height/view.zoom))};
            canvas_.SetClip(Box(bounds));
            canvas_.BlitRgba(Box(bounds),mapArt->rgba.data(),int(mapArt->width),int(mapArt->height),crop);
            auto valid=[](float x,float y) { return std::isfinite(x)&&std::isfinite(y)&&x>=0&&x<=1&&y>=0&&y<=1; };
            if (s.map.markers.known) for (size_t i=0;i<s.map.markers.size();++i) {
                const auto& marker=s.map.markers.entries[i];
                if (!marker.visible || !valid(marker.x,marker.y)) continue;
                auto point=view.Project(marker.x,marker.y);
                if (!bounds.Contains(point[0],point[1])) continue;
                int x=int(point[0]),y=int(point[1]);
                if (marker.kind==MarkerKind::Objective) {
                    canvas_.Line(x,y-12,x+12,y,3,Yellow); canvas_.Line(x+12,y,x,y+12,3,Yellow);
                    canvas_.Line(x,y+12,x-12,y,3,Yellow); canvas_.Line(x-12,y,x,y-12,3,Yellow);
                } else if (marker.kind==MarkerKind::Enemy) {
                    dis::Color red{239,102,90};
                    canvas_.Line(x-9,y-8,x+9,y-8,3,red); canvas_.Line(x+9,y-8,x,y+10,3,red); canvas_.Line(x,y+10,x-9,y-8,3,red);
                } else canvas_.StrokeCircle(x,y,8,2,Cyan);
            }
            if (s.map.playerX.known && s.map.playerY.known && valid(s.map.playerX.value,s.map.playerY.value)) {
                auto point=view.Project(s.map.playerX.value,s.map.playerY.value);
                if (bounds.Contains(point[0],point[1])) {
                    int x=int(point[0]),y=int(point[1]); canvas_.StrokeCircle(x,y,20,2,Cyan);
                    if (s.map.facing.known && std::isfinite(s.map.facing.value)) {
                        // Adapter contract: clockwise radians from map-up, after verified projection.
                        float angle=s.map.facing.value;
                        int tx=x+int(std::sin(angle)*17),ty=y-int(std::cos(angle)*17);
                        int lx=x+int(std::sin(angle+2.5f)*12),ly=y-int(std::cos(angle+2.5f)*12);
                        int rx=x+int(std::sin(angle-2.5f)*12),ry=y-int(std::cos(angle-2.5f)*12);
                        canvas_.Line(tx,ty,lx,ly,3,Cyan); canvas_.Line(tx,ty,rx,ry,3,Cyan); canvas_.Line(lx,ly,rx,ry,3,Cyan);
                    } else canvas_.FillCircle(x,y,5,Cyan);
                }
            }
            canvas_.ClearClip();
        }
        Label({54,878,1132,44},s.mission.str().empty()?"MISSION --":s.mission.str(),25,Cyan);
        Label({54,944,1132,42},s.location.str().empty()?"LOCATION --":s.location.str(),20,Muted);
    } else if (navigation.page==Page::Weapons) {
        for (int i=0;i<2;++i) {
            const auto slice=FindWeaponPanel(s.weapons,size_t(scroll+i));
            const Rect r=WeaponPanelBounds(i);
            Panel(r,slice.valid?"WEAPON "+std::to_string(slice.weapon+1)+" / "+
                (slice.attachmentStart?"ATTACHMENTS CONTINUED":"LOADOUT"):"WEAPON / LOADOUT");
            if (!slice.valid) { Empty(r,s.weapons.known&&s.weapons.complete?"NO ADDITIONAL EQUIPMENT":"EQUIPMENT DATA UNAVAILABLE"); continue; }
            const auto& w=s.weapons.entries[slice.weapon];
            Label({60,r.y+52,710,44},w.name.str(),32,Cyan);
            const auto* model=artwork.Model(w.icon.str());
            if(model) canvas_.BlitRgba(Box(model->Fit({60,r.y+94,730,244})),model->rgba.data(),int(model->width),int(model->height),
                {0,0,int(model->width),int(model->height)});
            Label({832,r.y+154,344,30},w.category.str(),18,Muted);
            Label({832,r.y+208,344,32},"MAGAZINE  "+Count(w.magazine),22);
            Label({832,r.y+260,344,32},"RESERVE   "+Count(w.reserve),22);
            Button({984,r.y+62,196,68},w.equipped.known&&w.equipped.value?"EQUIPPED":"EQUIP",w.select,s,now);
            if (!w.attachments.known) Label({60,r.y+358,1100,48},"ATTACHMENT DATA UNAVAILABLE",20,Muted);
            else if (!w.attachments.size()) Label({60,r.y+358,1100,48},"NO ATTACHMENTS REPORTED",20,Muted);
            for (size_t a=slice.attachmentStart;a<std::min(slice.attachmentStart+AttachmentsPerPanel,w.attachments.size());++a) {
                const size_t cell=a-slice.attachmentStart;
                const auto& attachment=w.attachments.entries[a];
                const std::string mounted=attachment.equipped.known && attachment.equipped.value?"ON / ":
                    attachment.owned.known && !attachment.owned.value &&
                    attachment.defaultEntry.known && !attachment.defaultEntry.value?"NOT OWNED / ":"";
                Button({60+float(cell)*378,r.y+358,364,64},mounted+attachment.name.str(),attachment.select,s,now);
            }
        }
    } else if (navigation.page==Page::Suit) {
        Panel({40,98,1160,188},"NANOSUIT STATE");
        Meter({60,159,424,90},"ENERGY",s.energy,s.maxEnergy);
        Button(ArmorButton,std::string("ARMOR / ")+State(s.armor),s.toggleArmor,s,now);
        Button(CloakButton,std::string("CLOAK / ")+State(s.cloak),s.toggleCloak,s,now);
        const bool suitArt=!artwork.suit.rgba.empty();
        const float moduleX=suitArt?400.0f:40.0f;
        if (suitArt) {
            Panel(SuitFigurePanel,"NANOSUIT");
            const auto& art=artwork.suit;
            // Owner-requested wider figure presentation within the reserved
            // panel; the original game bitmap is unchanged. Not a live 3D view.
            canvas_.BlitRgba(Box(SuitFigureArea),art.rgba.data(),int(art.width),int(art.height),
                {0,0,int(art.width),int(art.height)},{115,211,221,210});
            Label({56,954,308,36},"GAME SCHEMATIC",17,Muted);
        }
        Panel({moduleX,306,1200-moduleX,698},"MODULES / UPGRADES        POINTS: "+Count(s.upgradePoints));
        if (!s.modules.known) Empty({moduleX,356,1200-moduleX,620},"MODULE STATE UNAVAILABLE\nInstalled modules and progression will come from the game.");
        const size_t start=std::min<size_t>(scroll,s.modules.size()?s.modules.size()-1:0);
        for (size_t i=start;i<std::min(start+VisibleModules,s.modules.size());++i) {
            const auto& m=s.modules.entries[i]; float y=374+float(i-start)*150;
            const auto* icon=artwork.Icon("modules/"+m.icon.str());
            const float textX=moduleX+(icon?108:20);
            if(icon) canvas_.BlitRgba(Box(icon->Fit({moduleX+20,y,76,76})),icon->rgba.data(),int(icon->width),int(icon->height),
                {0,0,int(icon->width),int(icon->height)},{133,235,234,255});
            Label({textX,y,810-textX,38},m.name.str(),25,Cyan);
            Label({textX,y+38,810-textX,32},m.description.str(),18,Muted);
            const dis::Box track{int(moduleX+20),int(y+89),int(790-moduleX),9};
            canvas_.SetClip(track); canvas_.Bar(track,Ratio(m.progress,m.target),Cyan,{28,48,51}); canvas_.ClearClip();
            const bool owned=m.purchased.known&&m.purchased.value;
            const bool installed=m.installed.known&&m.installed.value;
            const std::string moduleLabel=s.readOnlyTrial?(installed?"INSTALLED":owned?"OWNED":"COST / "+Count(m.cost)):
                owned?(installed?"INSTALLED / REMOVE":"INSTALL"):"UNLOCK / "+Count(m.cost);
            Button({832,y+13,348,74},moduleLabel,owned?m.toggle:m.purchase,s,now);
        }
    } else if (navigation.page==Page::Objectives) {
        Panel(Content,"MISSION INTELLIGENCE");
        Label({50,148,1136,46},s.mission.str().empty()?"MISSION UNAVAILABLE":s.mission.str(),30,Cyan);
        Label({50,204,1136,34},s.location.str(),20,Muted);
        if (!s.objectives.known) Empty({40,256,1160,740},"OBJECTIVES UNAVAILABLE\nWaiting for the native mission state.");
        const size_t start=std::min<size_t>(scroll,s.objectives.size()?s.objectives.size()-1:0);
        for (size_t i=start;i<std::min(start+VisibleObjectives,s.objectives.size());++i) {
            const auto& o=s.objectives.entries[i]; float y=266+float(i-start)*144;
            canvas_.StrokeRect({52,int(y+15),22,22},2,o.state==ObjectiveState::Active?Yellow:Muted);
            if (o.state==ObjectiveState::Complete) canvas_.FillRect({57,int(y+20),12,12},Cyan);
            if (o.state==ObjectiveState::Failed) {
                canvas_.Line(56,int(y+19),70,int(y+33),2,{223,103,94});
                canvas_.Line(70,int(y+19),56,int(y+33),2,{223,103,94});
            }
            Label({92,y,1094,42},(o.state==ObjectiveState::Failed?"FAILED / ":"")+o.title.str(),24,o.state==ObjectiveState::Active?Yellow:Cyan);
            Label({92,y+44,1094,58},o.description.str(),19,Muted);
        }
    } else {
        Panel({40,98,1160,260},"VITAL SYSTEMS");
        Meter({60,170,532,88},"HEALTH",s.health,s.maxHealth);
        Meter({642,170,538,88},"SUIT ENERGY",s.energy,s.maxEnergy);
        Button(StatusArmorButton,std::string("ARMOR / ")+State(s.armor),s.toggleArmor,s,now);
        Button(StatusCloakButton,std::string("CLOAK / ")+State(s.cloak),s.toggleCloak,s,now);
        Panel({40,378,1160,626},"TRACKED STATISTICS");
        if (!s.statistics.known) Empty({40,430,1160,548},"STATISTICS UNAVAILABLE\nNo counters have been inferred or fabricated.");
        const size_t start=std::min<size_t>(scroll,s.statistics.size()?s.statistics.size()-1:0);
        for (size_t i=start;i<std::min(start+VisibleStatistics,s.statistics.size());++i) {
            const auto& stat=s.statistics.entries[i]; float y=440+float(i-start)*90;
            Label({60,y,730,38},stat.name.str(),24,Cyan);
            Label({60,y+37,730,27},stat.scope.str(),17,Muted);
            Label({858,y,318,54},NumberText(stat.value)+" "+stat.unit.str(),25);
        }
    }
    canvas_.Line(24,1032,1216,1032,1,Border);
    Label({26,1040,1000,28},navigation.page==Page::Map?"SWIPE HEADER / FOOTER TO CHANGE PAGE   /   DRAG MAP TO PAN":"SWIPE LEFT / RIGHT TO CHANGE PAGE   /   SWIPE VERTICALLY TO SCROLL",17,Muted);
    for (int i=0;i<int(Page::Count);++i) canvas_.FillRect({1083+i*25,1053,14,4},i==page?Cyan:Border);
}
} // namespace c3

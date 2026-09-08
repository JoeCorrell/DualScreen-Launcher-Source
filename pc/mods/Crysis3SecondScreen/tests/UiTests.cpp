#include "../src/Ui.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <memory>

int main(int argc,char** argv) {
    assert(argc==2);
    const std::filesystem::path folder(argv[1]); std::filesystem::create_directories(folder);
    const auto path=folder/"TEST-ONLY-map.rgba";
    { // Synthetic test image is never published as a game asset or screenshot.
        std::ofstream output(path,std::ios::binary);
        const uint32_t header[]{0x41523343,1,4,4};
        output.write(reinterpret_cast<const char*>(header),sizeof(header));
        for(int i=0;i<16;++i) { const unsigned char pixel[]{51,67,83,255}; output.write(reinterpret_cast<const char*>(pixel),4); }
    }
    c3::Artwork image; assert(image.Load(path)); assert(image.width==4 && image.height==4);
    assert(!image.Load(folder/"missing.rgba")); assert(!image.width && image.rgba.empty());
    const auto bad=folder/"TEST-ONLY-bad.rgba";
    { std::ofstream output(bad,std::ios::binary); const uint32_t header[]{0x41523343,1,0xffffffff,4}; output.write(reinterpret_cast<const char*>(header),sizeof(header)); }
    assert(!image.Load(bad));
    auto ui=std::make_unique<c3::Ui>(); assert(ui->Create());
    assert(c3::Content.y==82 && c3::Content.y+c3::Content.h==1020);
    assert(c3::MapArea.y>=c3::Content.y+44 && c3::MapArea.y+c3::MapArea.h<1020);
    assert(c3::Content.Contains(c3::ArmorButton.x,c3::ArmorButton.y));
    assert(c3::SuitFigurePanel.w==340 && c3::SuitFigureArea.h==600);
    c3::Artwork suitShape; suitShape.width=128; suitShape.height=512;
    const auto suitFit=suitShape.Fit(c3::SuitFigureArea);
    assert(suitFit.w==150 && suitFit.h==600); // Generic fit preserves aspect; suit UI explicitly widens its presentation.
    assert(c3::SuitFigurePanel.Contains(suitFit.x,suitFit.y));
    assert(c3::SuitFigurePanel.Contains(suitFit.x+suitFit.w-1,suitFit.y+suitFit.h-1));
    for (int i=0;i<2;++i) {
        const auto panel=c3::WeaponPanelBounds(i);
        assert(c3::Content.Contains(panel.x,panel.y));
        assert(c3::Content.Contains(panel.x+panel.w-1,panel.y+panel.h-1));
        assert(panel.y+358+64<panel.y+panel.h); // large preview plus one touch-friendly attachment row
    }
    assert(ui->artwork.AddMap(1,path)); assert(!ui->artwork.AddMap(1,path)); assert(!ui->artwork.AddMap(0,path));
    assert(ui->artwork.AddIcon("weapons/TEST",path));
    assert(!ui->artwork.AddIcon("weapons/TEST",path));
    assert(!ui->artwork.AddIcon("",path));
    assert(ui->artwork.Icon("weapons/TEST") && !ui->artwork.Icon("weapons/UNKNOWN"));
    auto s=std::make_unique<c3::Snapshot>();
    s->generation=1; s->sequence=1; s->sampledAt=1000; s->adapterVerified=true; s->phase=c3::Phase::Gameplay;
    s->map.artwork=1; s->map.calibrated=true; s->map.playerX={0.5f,true}; s->map.playerY={0.5f,true};
    s->map.facing={0,true}; s->map.markers.known=true; s->map.markers.count=2;
    s->map.markers.entries[0]={2,0.25f,0.25f,c3::MarkerKind::Enemy,false};
    s->map.markers.entries[1]={3,std::numeric_limits<float>::quiet_NaN(),0.5f,c3::MarkerKind::Objective,true};
    ui->Render(*s,1000); assert(ui->navigation.mapReady);
    auto bounds=ui->navigation.map.viewport; assert(bounds.w==bounds.h);
    auto point=ui->navigation.map.Project(0.25f,0.25f);
    assert(GetPixel(ui->Canvas().dc(),int(point[0]),int(point[1]))==RGB(51,67,83));
    auto before=ui->navigation.map.Project(0.4f,0.6f);
    ui->navigation.map.Pinch(before[0],before[1],2);
    auto after=ui->navigation.map.Project(0.4f,0.6f);
    assert(std::abs(before[0]-after[0])<0.01f && std::abs(before[1]-after[1])<0.01f);
    s->map.markers.entries[0].visible=true;
    ui->navigation.map={}; ui->Render(*s,1000);
    point=ui->navigation.map.Project(0.25f,0.25f);
    assert(GetPixel(ui->Canvas().dc(),int(point[0]),int(point[1])-8)==RGB(239,102,90));
    ui->Render(*s,1751); assert(!ui->navigation.mapReady);
    assert(GetPixel(ui->Canvas().dc(),int(point[0]),int(point[1])-8)!=RGB(239,102,90));
    ui->navigation.page=c3::Page::Suit;
    s->toggleArmor={9,c3::Action::ToggleArmor,true,true};
    ui->Render(*s,1000);
    auto action=ui->Tap(600,206,*s,1000); assert(action && c3::Eligible(*s,*action,1000));
    assert(!ui->Tap(600,206,*s,1001)); // duplicate up/tap suppression
    ++s->sequence; assert(!ui->Tap(600,206,*s,1200)); // stale visual hit targets
    ui->Render(*s,1200); assert(ui->Tap(600,206,*s,1200));
    s->phase=c3::Phase::Paused; ui->Render(*s,1400); assert(!ui->Tap(600,206,*s,1400));
    s->phase=c3::Phase::Gameplay; ui->Render(*s,1500);
    ui->navigation.Next(1); assert(!ui->Tap(600,206,*s,1600));
    ui->navigation.page=c3::Page::Weapons;
    s->weapons.known=true; s->weapons.count=2;
    auto& weapon=s->weapons.entries[0]; weapon.attachments.known=true; weapon.attachments.count=12;
    for (size_t i=0;i<12;++i) weapon.attachments.entries[i].select={100+i,c3::Action::EquipAttachment,true,true};
    assert(c3::WeaponPanelCount(s->weapons)==5);
    assert(c3::FindWeaponPanel(s->weapons,1).attachmentStart==3);
    assert(c3::FindWeaponPanel(s->weapons,4).weapon==1);
    assert(!c3::FindWeaponPanel(s->weapons,5).valid);
    s->sampledAt=2000; ui->Render(*s,2000);
    action=ui->Tap(1000,926,*s,2000); assert(action && action->capability.key==105);
    ui->navigation.scroll[int(c3::Page::Weapons)]=3;
    assert(!ui->Tap(1000,926,*s,2200)); // scrolling invalidates old hit regions
    ui->Render(*s,2200);
    action=ui->Tap(1000,466,*s,2200); assert(action && action->capability.key==111);
    ui->navigation.scroll[int(c3::Page::Weapons)]=63; ui->Render(*s,2300);
    assert(ui->navigation.scroll[int(c3::Page::Weapons)]==3);
    // A fresh frame may update counts while held, but may not replace the owner.
    s->sampledAt=3000; ui->Render(*s,3000);
    ui->PointerDown(1000,466,*s,3000);
    ++s->sequence; ui->Render(*s,3001);
    assert(ui->PointerUp(1000,466,*s,3001));
    ui->PointerDown(1000,466,*s,3200);
    weapon.attachments.entries[11].select.key=999;
    ++s->sequence; ui->Render(*s,3201);
    assert(!ui->PointerUp(1000,466,*s,3201));
    ui->PointerDown(1000,466,*s,3400);
    ui->PointerMove(900,600); ui->PointerMove(1000,466);
    assert(!ui->PointerUp(1000,466,*s,3401));
    ui->PointerDown(1000,466,*s,3500); ui->PointerCancel();
    assert(!ui->PointerUp(1000,466,*s,3501));
    // Even a producer violating sequence discipline cannot grant an old target.
    weapon.attachments.entries[11].select={};
    assert(!ui->Tap(1000,466,*s,3600));
    s->sampledAt=2300;
    ui->navigation.mapReady=true; const float previousZoom=ui->navigation.map.zoom;
    ui->navigation.Down(600,400); ui->navigation.Pinch(600,400,2);
    assert(!ui->navigation.Up(400,400)); assert(ui->navigation.page==c3::Page::Weapons);
    assert(ui->navigation.map.zoom==previousZoom); // non-map pinch cancels, never zooms
    ui->navigation.page=c3::Page::Status; ui->Render(*s,2300);
    for (int y=216;y<230;++y) for (int x=592;x<612;++x)
        assert(GetPixel(ui->Canvas().dc(),x,y)==RGB(7,17,20)); // no hatch bleed into the meter gutter
    // Reclaimed banner/footer space increases real list capacity, not blank padding.
    s->modules.known=true; s->modules.count=16;
    s->objectives.known=true; s->objectives.count=64;
    s->resources.known=true; s->resources.count=48;
    s->statistics.known=true; s->statistics.count=48;
    for (const auto page:{c3::Page::Suit,c3::Page::Objectives,c3::Page::Status}) {
        ui->navigation.page=page; ui->navigation.scroll[int(page)]=63;
        ui->Render(*s,2300);
        const int expected=page==c3::Page::Suit?12:page==c3::Page::Objectives?59:42;
        assert(ui->navigation.scroll[int(page)]==expected);
    }
    // Compact heading and bottom hint remain swipe regions, never page buttons.
    ui->navigation.page=c3::Page::Map; ui->Render(*s,2300);
    ui->PointerDown(900,40,*s,2300); ui->PointerMove(500,40);
    assert(!ui->PointerUp(500,40,*s,2300)); assert(ui->navigation.page==c3::Page::Weapons);
    ui->Render(*s,2300);
    ui->PointerDown(500,1050,*s,2300); ui->PointerMove(900,1050);
    assert(!ui->PointerUp(900,1050,*s,2300)); assert(ui->navigation.page==c3::Page::Map);
    ui->Render(*s,2300);
    const auto mapPage=ui->navigation.page;
    ui->PointerDown(650,500,*s,2300); ui->PointerMove(450,500);
    assert(!ui->PointerUp(450,500,*s,2300)); assert(ui->navigation.page==mapPage);
    // Both pages route suit controls through the SAME guarded capabilities.
    s->sampledAt=10000;s->phase=c3::Phase::Gameplay;s->readOnlyTrial=false;
    s->toggleArmor={601,c3::Action::ToggleArmor,true,true};
    s->toggleCloak={602,c3::Action::ToggleCloak,true,true};
    ui->navigation.page=c3::Page::Status;ui->Render(*s,10000);
    action=ui->Tap(200,300,*s,10000);assert(action&&action->capability.key==601);
    s->sampledAt=10500;ui->Render(*s,10500);
    action=ui->Tap(900,300,*s,10500);assert(action&&action->capability.key==602);
    s->readOnlyTrial=true;s->sampledAt=11000;ui->Render(*s,11000);
    assert(!ui->Tap(200,300,*s,11000)); // Never enables a native action just by drawing a button.
    std::cout<<"PASS: artwork bounds, map aspect/anchor, hidden and invalid markers, stale frame removal, action bounds and duplicate guards\n";
}

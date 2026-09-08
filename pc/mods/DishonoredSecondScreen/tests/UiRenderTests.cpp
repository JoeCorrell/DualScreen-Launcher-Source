// Offline visual fixtures only. None of this data is linked into the payload.
#include "../src/Ui.h"
#include "../src/JournalIntegration.h"
#include "../src/ItemCatalog.h"
#include "../src/MapCalibration.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <cmath>
namespace dishonored {
struct UiTestAccess {
    static void MapTargets(Ui& ui, bridge::Snapshot& state, Bridge& bridge, uint64_t now) {
        assert(ui.mapView_.markers.domain.stored==2);
        auto hit=std::find_if(ui.hits_.begin(),ui.hits_.end(),[](const auto& h){return h.kind==Ui::HitKind::Marker;});
        assert(hit!=ui.hits_.end());
        ui.Activate(*hit,now);
        assert(!ui.feedback_.empty());
        bridge::Command command{};assert(!bridge.commands.Pop(command));
        ui.Update(state,now+751);assert(ui.dirty_);ui.Render(now+751);
        assert(!ui.mapView_.markers.domain.stored);
        for(auto& target:state.objectiveTargets.entries)if(target.task)target.observedAt=now+752;
        ++state.sequence;state.sampledAt=now+752;
        ui.Update(state,now+752);assert(ui.dirty_);ui.Render(now+752);
        assert(ui.mapView_.markers.domain.stored==2);
        puts("PASS: rendered objective marker tap, no gameplay command, paused expiry and renewed observation redraw");
    }
    static void InventoryAndPause(Ui& ui, bridge::Snapshot& state, Bridge& bridge) {
        auto saved = std::make_unique<bridge::Snapshot>(state);
        uint64_t now = 60000;
        state.sampledAt = now; ++state.sequence;
        state.equipment.entries[0].selected = {false, true};
        state.equipment.entries[0].selectable = {true, true};
        state.equipment.entries[1].kind = bridge::EquipmentKind::Consumable;
        ++state.equipment.domain.revision;
        ui.pendingRequest_ = 0; ui.selected_.fill(0); ui.equipmentFilter_ = 0;
        Page(ui, bridge::Page::Equipment); ui.Update(state, now); ui.Render(now);
        assert(ui.listBox_.y == 208 && ui.listBox_.h == 728);
        const auto touch = [&](Gesture gesture, int x, int y) {
            assert(ui.Input({gesture, float(x)/1240, float(y)/1080, 1}, ++now));
        };
        bridge::Command command{};
        touch(Gesture::Down, 220, 300); touch(Gesture::Up, 220, 300);
        assert(bridge.commands.Pop(command) && command.kind == bridge::CommandKind::SelectEquipment && command.key == state.equipment.entries[0].key);
        assert(!state.equipment.entries[0].selected.value && !ui.selected_[2]);
        assert(ui.rows_[0].annotation == L"Owned"); // No optimistic Equipped label.
        bridge.acknowledgements.Push({command.request, state.generation, bridge::CommandResult::Applied});
        state.equipment.entries[0].selected = {true, true}; ++state.equipment.domain.revision;
        state.sampledAt = ++now; ++state.sequence; ui.Update(state, now); ui.Render(now);
        assert(ui.rows_[0].annotation == L"Equipped");
        touch(Gesture::Down, 220, 300); touch(Gesture::Up, 220, 300);
        assert(!bridge.commands.Pop(command)); // Already equipped is a no-op.
        touch(Gesture::Hold, 220, 300); ui.Render(now);
        assert(ui.selected_[2] && !bridge.commands.Pop(command)); // Hold opens details.
        ui.selected_[2] = 0; ui.dirty_ = true; ui.Render(now);
        touch(Gesture::Down, 620, 300); touch(Gesture::Up, 620, 300); ui.Render(now);
        assert(ui.selected_[2] == state.equipment.entries[1].key && !bridge.commands.Pop(command));
        ui.selected_[2] = 0; ui.dirty_ = true; ui.Render(now);
        touch(Gesture::Down, 1000, 300); touch(Gesture::Move, 1000, 500); touch(Gesture::Up, 1000, 500);
        assert(!bridge.commands.Pop(command));
        // Paused game: no new samples for a minute. Navigation, documents and
        // scrolling stay available, while native actions cannot use old state.
        now += 60000; ui.Update(state, now);
        for (unsigned page = 0; page < 7; ++page) {
            Page(ui, bridge::Page(page)); ui.Render(now); Bounds(ui);
            assert(!ui.fresh_ && ui.snapshot_->lifecycle == bridge::Lifecycle::Live);
        }
        Page(ui, bridge::Page::Journal); ui.journalFilter_ = 0; ui.Render(now);
        touch(Gesture::Down, 220, 280); touch(Gesture::Up, 220, 280); ui.Render(now);
        assert(ui.selected_[4] && ui.detailBox_.h > 0 && !bridge.commands.Pop(command));
        Page(ui, bridge::Page::Equipment); ui.Render(now);
        touch(Gesture::Down, 220, 300); touch(Gesture::Up, 220, 300); ui.Render(now);
        assert(ui.selected_[2] && !bridge.commands.Pop(command));
        for (const auto& hit : ui.hits_) if (hit.kind == Ui::HitKind::Action) assert(!hit.enabled);
        state.lifecycle = bridge::Lifecycle::NoPlayer; ++state.generation; ++state.sequence;
        ui.Update(state, now); ui.Render(now); assert(ui.hits_.empty());
        state = *saved; ui.pendingRequest_ = 0; ui.Cancel();
        puts("PASS: tap-to-equip, confirmed Equipped labels, hold details, no accidental consumable use, drag isolation, paused browsing and lifecycle invalidation");
    }
    static void MapBrowsing(Ui& ui, bridge::Snapshot& state, Bridge& bridge) {
        auto saved = std::make_unique<bridge::Snapshot>(state);
        auto& map = state.map;
        map.sheets.domain.stored = map.sheets.domain.total = 2;
        map.sheets.entries[0].artwork = map.label.artwork;
        auto& other = map.sheets.entries[1]; other = {};
        other.key = 7; other.title = state.journal.entries[0].label.title; other.artwork.id = 2007;
        other.legend.domain = {bridge::Availability::Available, 1, 1, 1};
        other.legend.entries[0] = {20, state.journal.entries[0].contents};
        map.playerX = {0.5f, true}; map.playerY = {0.5f, true}; map.facingRadians = {1, true};
        map.markers.domain.stored = 1;
        ++map.domain.revision; ++state.sequence;
        uint64_t now = state.sampledAt + 60000;
        ui.preferredMap_ = 0; ui.pendingRequest_ = 0;
        Page(ui, bridge::Page::Map); ui.Update(state, now); ui.Render(now);
        ui.Activate({{}, Ui::HitKind::MapSheet, 7, 0, true}, ++now); ui.Render(now);
        assert(!ui.fresh_ && ui.preferredMap_ == 7 && ui.mapView_.key == 7);
        assert(ui.mapView_.label.artwork.id == 2007 && !ui.mapView_.followingPlayer);
        assert(ui.mapView_.legend.domain.stored == 1 && ui.mapView_.legend.entries[0].symbol == 20);
        assert(!ui.mapView_.playerX.known && !ui.mapView_.playerY.known && !ui.mapView_.markers.domain.stored);
        // A calibrated sheet carries its own real pose. Motion must repaint
        // even though the map collection/revision and selected sheet are stable.
        other.playerX = {0.4f, true}; other.playerY = {0.6f, true}; other.positionApproximate = true;
        ++state.sequence; ui.Update(state, ++now); assert(ui.dirty_); ui.Render(now); Bounds(ui);
        assert(ui.mapView_.playerX.known && std::abs(ui.mapView_.playerX.value - 0.4f) < 0.00001f &&
            std::abs(ui.mapView_.playerY.value - 0.6f) < 0.00001f && ui.mapView_.positionApproximate && !ui.mapView_.facingRadians.known);
        other.facingRadians = {1.2f, true}; ++state.sequence;
        ui.Update(state, ++now); assert(ui.dirty_); ui.Render(now);
        assert(ui.mapView_.facingRadians.known && std::abs(ui.mapView_.facingRadians.value - 1.2f) < 0.00001f);
        bridge::Command command{}; assert(!bridge.commands.Pop(command));
        ui.Activate({{}, Ui::HitKind::MapSheet, 13, 0, true}, ++now);
        assert(ui.preferredMap_ == 7); // Unacquired sheet rejected, including synthetic stale hit.
        ui.Activate({{}, Ui::HitKind::MapSheet, 0, 0, true}, ++now); ui.Render(now);
        assert(!ui.preferredMap_ && ui.mapView_.key == map.key && ui.mapView_.playerX.known);
        ui.Activate({{}, Ui::HitKind::MapSheet, 7, 0, true}, ++now); ui.Render(now);
        map.sheets.domain.stored = 1; map.sheets.domain.availability = bridge::Availability::Incomplete;
        ++map.domain.revision; ++state.sequence;
        ui.Update(state, ++now); ui.Render(now);
        assert(ui.preferredMap_ == 7 && !ui.mapView_.label.artwork.id && !ui.mapView_.acquired.known);
        map.sheets.domain.availability = bridge::Availability::Available;
        ++map.domain.revision; ++state.sequence;
        ui.Update(state, ++now); ui.Render(now);
        assert(!ui.preferredMap_ && ui.mapView_.key == map.key); // Revalidate every snapshot.
        map.sheets.domain.stored = 2; ++map.domain.revision;
        ui.Update(state, ++now);
        ui.Activate({{}, Ui::HitKind::MapSheet, 7, 0, true}, ++now); ui.Render(now);
        ++state.generation; ++state.sequence; ui.Update(state, ++now); ui.Render(now);
        assert(!ui.preferredMap_ && !bridge.commands.Pop(command));
        state = *saved; ui.Cancel();
        puts("PASS: paused map selection/legends, no game commands, cross-map marker isolation, acquisition and generation invalidation");
    }
    static void KeyRingReader(Ui& ui, bridge::Snapshot& state, Bridge& bridge) {
        auto saved = std::make_unique<bridge::Snapshot>(state);
        auto& key = state.equipment.entries[state.equipment.domain.stored++];
        key = {}; key.key = KeyRingKeyPrefix | 2; key.kind = bridge::EquipmentKind::Key;
        key.label = state.journal.entries[0].label;
        assert(AppendKeyRingJournal(state));
        ++state.journal.domain.revision; ++state.sequence; state.sampledAt = 1600;
        Page(ui, bridge::Page::Journal); ui.journalFilter_ = 5;
        ui.Update(state, 1600); ui.Render(1600);
        assert(ui.rows_.size() == 1 && ui.rows_[0].key == key.key && !ui.rows_[0].actionable);
        auto hit = std::find_if(ui.hits_.begin(), ui.hits_.end(), [&](const Ui::Hit& h) { return h.key == key.key; });
        assert(hit != ui.hits_.end()); const auto box = hit->box;
        ui.Input({Gesture::Down, float(box.x + box.w / 2) / 1240, float(box.y + box.h / 2) / 1080, 1}, 1601);
        ui.Input({Gesture::Up, float(box.x + box.w / 2) / 1240, float(box.y + box.h / 2) / 1080, 1}, 1610);
        ui.Render(1610); Bounds(ui);
        assert(ui.selected_[uint32_t(bridge::Page::Journal)] == key.key);
        bridge::Command command{}; assert(!bridge.commands.Pop(command));
        ui.journalFilter_ = 0; state = *saved; ui.Cancel();
        puts("PASS: native key-ring entry opens in Journal Keys with no item getter or gameplay command");
    }
    static void FullStatistics(Ui& ui, bridge::Snapshot& state) {
        auto saved = std::make_unique<bridge::Snapshot>(state);
        for (uint32_t i = 0; i < bridge::StatisticCapacity; ++i) {
            auto& entry = state.statistics.entries[i];
            entry.key = i + 1; entry.scope = i < 19 ? bridge::StatisticScope::CurrentMission : bridge::StatisticScope::SavedMission;
            entry.label = saved->statistics.entries[0].label; entry.value = {float(i), true};
        }
        state.statistics.domain = {bridge::Availability::Incomplete, 900, bridge::StatisticCapacity, bridge::StatisticCapacity};
        state.sampledAt = 1500; ++state.sequence;
        Page(ui, bridge::Page::Statistics); ui.savedStatistics_ = true;
        ui.Update(state, 1500); ui.Render(1500);
        auto& scroll = ui.listScroll_[uint32_t(bridge::Page::Statistics)];
        assert(scroll.maximum == 144 * 144 - 16 - 624);
        scroll.offset = scroll.maximum; ui.dirty_ = true; ui.Render(1500); Bounds(ui);
        assert(scroll.offset == scroll.maximum);
        ui.savedStatistics_ = false; scroll = {}; state = *saved;
        puts("PASS: all 288 saved-result rows remain scrollable beyond the former 256-record limit");
    }
    static void Responsiveness(Ui& ui, bridge::Snapshot& state) {
        Page(ui,bridge::Page::Powers);ui.selected_[1]=0;ui.listScroll_[1]={};state.sampledAt=1200;++state.sequence;
        ui.Update(state,1200);ui.Render(1200);ui.Update(state,1200);ui.Render(1200);
        assert(!ui.Render(1201));
        ++state.sequence;state.sampledAt=1250;ui.Update(state,1250);
        assert(!ui.Render(1250));
        const auto mission=state.mission, area=state.area;
        state.mission=area;state.area=mission;++state.sequence;ui.Update(state,1250);
        assert(!ui.Render(1250)); // Removed header fields do not trigger repaint.
        state.mission=mission;state.area=area;
        ++state.objectives.domain.revision;++state.sequence;state.sampledAt=1300;
        ui.Update(state,1300);assert(!ui.Render(1300));
        --state.health.value;++state.sequence;state.sampledAt=1350;
        ui.Update(state,1350);assert(ui.Render(1350));
        const uint64_t previousFrame=ui.frameAt_;
        ui.Input({Gesture::Down,222.0f/1240,240.0f/1080,1},1351);
        ui.Input({Gesture::Up,222.0f/1240,240.0f/1080,1},1360);
        assert(ui.frameAt_==previousFrame && ui.selected_[1]);
        ui.Render(1360);
        std::vector<uint8_t> cached(1240*1080*4),uncached(cached.size());
        ui.Canvas().CopyTo(cached.data(),1240*4);ui.backdropReady_=false;ui.dirty_=true;ui.Render(1360);
        ui.Canvas().CopyTo(uncached.data(),1240*4);assert(cached==uncached);
        ui.selected_[1]=0;ui.Cancel();++state.health.value;
        puts("PASS: unchanged snapshots and off-page updates skip rasterization; health repaints; release precedes render; cached backdrop is pixel-identical");
    }
    static void Page(Ui& ui, bridge::Page page) { ui.ChangePage(page); ui.transitioning_=false; }
    static void Select(Ui& ui) { if(!ui.rows_.empty()) ui.selected_[uint32_t(ui.page_)]=ui.rows_.front().key; ui.dirty_=true; }
    static void Scroll(Ui& ui) { ui.listScroll_[uint32_t(ui.page_)].offset=91;ui.dirty_=true; }
    static void Bounds(Ui& ui) {
        for(const auto& hit:ui.hits_) assert(hit.box.x>=0 && hit.box.y>=0 && hit.box.w>0 && hit.box.h>0 && hit.box.right()<=1240 && hit.box.bottom()<=1080);
    }
    static void Footer(Ui& ui) {
        // Compare actual pixels: page content, selection and scrolling must never
        // overwrite the persistent health/mana labels, values or bars.
        std::vector<uint8_t> pixels(1240*1080*4);
        ui.Canvas().CopyTo(pixels.data(),1240*4);
        static std::vector<uint8_t> reference;
        auto first=pixels.begin()+936*1240*4;
        auto last=pixels.begin()+1000*1240*4;
        if(reference.empty()) reference.assign(first,last);
        else assert(std::equal(first,last,reference.begin()));
    }
    static void Interaction(Ui& ui, bridge::Snapshot& state, Bridge& bridge) {
        uint64_t now=2000;
        auto fresh=[&]() { state.sampledAt=now;++state.sequence;ui.Update(state,now);ui.Render(now); };
        auto event=[&](Gesture kind,float x,float y,float factor=1) { assert(ui.Input({kind,x/1240,y/1080,factor},now)); };
        auto finish=[&]() { now+=260;fresh(); };
        auto swipe=[&](float x,float y,float endX,float endY) {event(Gesture::Down,x,y);now+=40;event(Gesture::Move,endX,endY);now+=40;event(Gesture::Up,endX,endY);};
        Page(ui,bridge::Page::Powers);ui.selected_[1]=0;fresh();
        assert(ui.listBox_.y==120 && ui.listBox_.h==816 && ui.detailBox_.h==0);
        event(Gesture::Down,222,240);event(Gesture::Up,222,240);ui.Render(now);
        assert(ui.selected_[1] && ui.listBox_.h==536 && ui.detailBox_.h>0);
        auto close=std::find_if(ui.hits_.begin(),ui.hits_.end(),[](const Ui::Hit& h){return h.kind==Ui::HitKind::CloseDetails;});
        assert(close!=ui.hits_.end());auto box=close->box;
        event(Gesture::Down,box.x+box.w/2,box.y+box.h/2);event(Gesture::Up,box.x+box.w/2,box.y+box.h/2);ui.Render(now);
        assert(!ui.selected_[1] && ui.listBox_.h==816 && ui.detailBox_.h==0);
        swipe(700,350,400,355);assert(ui.page_==bridge::Page::Equipment && ui.transitioning_);
        ui.Render(now+80);assert(ui.transitioning_);finish();assert(!ui.transitioning_);
        swipe(400,350,700,350);assert(ui.page_==bridge::Page::Powers);finish();
        swipe(620,720,625,380);assert(ui.page_==bridge::Page::Powers && ui.listScroll_[1].offset>0);finish();
        Page(ui,bridge::Page::Map);fresh();
        swipe(700,400,300,405);assert(ui.page_==bridge::Page::Map);
        event(Gesture::Pinch,620,440,2);assert(ui.mapScale_==2);
        for(float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
            assert(!ui.Input({Gesture::Pinch,0.5f,0.4f,invalid},now));
        assert(ui.mapScale_==2 && std::isfinite(ui.mapX_) && std::isfinite(ui.mapY_));
        event(Gesture::Up,300,440);assert(ui.page_==bridge::Page::Map);
        swipe(700,50,300,50);assert(ui.page_==bridge::Page::Powers);finish();
        event(Gesture::Down,700,350);event(Gesture::Move,400,350);event(Gesture::Cancel,400,350);event(Gesture::Up,400,350);
        assert(ui.page_==bridge::Page::Powers);
        bridge::Command command{};assert(!bridge.commands.Pop(command));
        assert(!ui.Input({Gesture::Down,-1,0,1},now));
        puts("PASS: full-height powers, details open/close, both swipe directions, slide completion, vertical scroll, map pan/pinch, cancellation and zero gameplay commands");
    }
    static void Lifecycle(Ui& ui, bridge::Snapshot& state) {
        auto saved=std::make_unique<bridge::Snapshot>(state);
        state.charmSlots={10,true};state.sampledAt=4000;++state.sequence;
        ui.Update(state,4000);Page(ui,bridge::Page::BoneCharms);ui.Render(4000);Bounds(ui);Footer(ui);
        state.powers.domain.stored=0;state.sampledAt=4100;++state.sequence;
        ui.Update(state,4100);Page(ui,bridge::Page::Powers);ui.Render(4100);
        assert(ui.rows_.empty() && !ui.selected_[1] && ui.detailBox_.h==0 && ui.listBox_.h==816);
        state.lifecycle=bridge::Lifecycle::NoPlayer;++state.generation;++state.sequence;
        for(unsigned page=0;page<7;++page) {
            Page(ui,bridge::Page(page));ui.Update(state,50000);ui.Render(50000);
            assert(ui.hits_.empty());Bounds(ui);Footer(ui);
        }
        state=*saved;state.generation++;state.sampledAt=51000;++state.sequence;
        ui.Update(state,51000);Page(ui,bridge::Page::Powers);ui.Render(51000);
        assert(ui.fresh_ && !ui.rows_.empty() && !ui.selected_[1]);Bounds(ui);Footer(ui);
        puts("PASS: footer pixels unchanged across every page, details and scrolling; ten charm slots, empty powers, stale-state input suppression and new-generation recovery");
    }
};
}
using namespace dishonored;
bridge::TextRef Text(bridge::Snapshot& s,std::u16string_view text) {
    auto offset=s.textUsed;assert(offset+text.size()+1<s.text.size());
    std::copy(text.begin(),text.end(),s.text.begin()+offset);s.textUsed+=uint32_t(text.size()+1);
    return {offset,uint32_t(text.size())};
}
bridge::Label Label(bridge::Snapshot& s,std::u16string_view title,uint64_t icon=0) {
    return {Text(s,title),Text(s,u"An original game description can be read here. This deliberately long fixture checks wrapping, padding and scrolling.\n\nEvery description stays inside its panel, including longer localized entries and mission details."),{icon}};
}
void Save(const Ui& ui,const std::filesystem::path& path) {
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+1240*1080*4;
    BITMAPINFOHEADER bitmap{};bitmap.biSize=sizeof(bitmap);bitmap.biWidth=1240;bitmap.biHeight=-1080;bitmap.biPlanes=1;bitmap.biBitCount=32;
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&bitmap),sizeof(bitmap));
    std::vector<uint8_t> pixels(1240*1080*4);ui.Canvas().CopyTo(pixels.data(),1240*4);
    for(size_t i=0;i<pixels.size();i+=4)std::swap(pixels[i],pixels[i+2]);
    out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());assert(out.good());
}
int wmain(int argc,wchar_t** argv) {
    assert(argc==3);std::filesystem::create_directories(argv[2]);
    Bridge bridge;bridge.bootstrap=Bridge::Bootstrap::Ready;auto state=std::make_unique<bridge::Snapshot>();auto& s=*state;
    s.generation=1;s.sequence=1;s.sampledAt=1000;s.lifecycle=bridge::Lifecycle::Live;
    s.health={70,true};s.maximumHealth={100,true};s.mana={60,true};s.maximumMana={100,true};s.coins={1254,true};s.runes={5,true};s.charmSlots={6,true};
    s.mission=Text(s,u"The Royal Physician");s.area=Text(s,u"Distillery District");s.chapter=Label(s,u"The Royal Physician");
    const char16_t* powers[]={u"Blink",u"Bend Time",u"Windblast",u"Possession",u"Dark Vision",u"Devouring Swarm",u"Vitality",u"Agility",u"Blood Thirsty",u"Shadow Kill"};
    for(unsigned i=0;i<10;++i) {auto& p=s.powers.entries[i];p.key=100+i;p.label=Label(s,powers[i],102+i);p.unlocked={i<6,true};p.selectable={i<6,true};p.selected={i==0,true};p.tier={1,true};p.maximumTier={2,true};p.manaCost={20,true};p.runeRequirement={3,true};}
    s.powers.domain={bridge::Availability::Available,1,10,10};
    const char16_t* items[]={u"Folding Blade",u"Pistol",u"Crossbow",u"Crossbow Bolts",u"Sleep Darts",u"Incendiary Bolts",u"Grenades",u"Spring Razor",u"Health Elixir",u"Mana Elixir",u"Pistol Rounds",u"Pistol Accuracy Upgrade"};
    for(unsigned i=0;i<12;++i) {auto& p=s.equipment.entries[i];p.key=200+i;p.label=Label(s,items[i],1000+i);p.kind=i<3?bridge::EquipmentKind::Weapon:bridge::EquipmentKind::Ammunition;p.quantity={5,true};p.capacity={10,true};p.selectable={true,true};p.selected={i==0||i==1,true};}
    s.equipment.domain={bridge::Availability::Available,1,12,12};
    const char16_t* itemArt[]={u"CorvoSword",u"Gun",u"Crossbow",u"CrossbowRegularBolt",u"CrossbowSleepDart",u"CrossbowFlareBolt",u"CorvoGrenade",u"SpringRazor",u"HealthElixir",u"ManaElixir",u"RegularBullets",u"Upg_Armor"};
    for(unsigned i=0;i<12;++i)s.equipment.entries[i].label.artwork.id=ItemArtwork(itemArt[i]);
    s.equipment.entries[8].kind=s.equipment.entries[9].kind=bridge::EquipmentKind::Consumable;
    s.equipment.entries[11].kind=bridge::EquipmentKind::Upgrade;
    for(unsigned i=0;i<18;++i) {auto& p=s.charms.entries[i];p.key=300+i;p.label=Label(s,u"Fleet Fighter",1010);p.equipped={i<3,true};p.equippedSlot={int(i),i<3};p.canEquip={true,true};p.canRemove={true,true};}
    s.charms.domain={bridge::Availability::Available,1,18,18};for(unsigned i=0;i<6;++i)s.equippedCharms[i]={i<3?300+i:0,true};
    for(unsigned i=0;i<18;++i)s.charms.entries[i].label.artwork.id=ItemArtwork(u"BoneCharms");
    for(unsigned i=0;i<9;++i){auto& p=s.objectives.entries[i];p.key=400+i;p.label=Label(s,i%2?u"Find Anton Sokolov and bring him to safety":u"Reach the Distillery District");p.state=i<3?bridge::ObjectiveState::Completed:bridge::ObjectiveState::Active;p.optional={i>4,true};}
    s.objectives.domain={bridge::Availability::Available,1,9,9};
    for(unsigned i=0;i<12;++i){auto& p=s.journal.entries[i];p.key=500+i;p.label=Label(s,u"A Letter from the Lord Regent");p.kind=bridge::JournalKind::Note;p.contents=p.label.description;p.contentsComplete=true;}
    s.journal.domain={bridge::Availability::Available,1,12,12};
    s.journal.entries[0].label.title=Text(s,u"Interrogation of the Royal Protector");
    s.journal.entries[0].label.artwork.id=ItemArtwork(u"Audiograph");
    s.journal.entries[0].kind=bridge::JournalKind::Audiograph;
    s.journal.entries[0].canPlay={true,true};s.journal.entries[0].playing={false,true};
    const char16_t* stats[]={u"Coins collected",u"Enemies killed",u"Non-lethal takedowns",u"Alarms triggered",u"Bodies discovered",u"Civilians killed",u"Bone charms discovered",u"Runes found",u"Collectibles found",u"Ghost",u"Clean Hands",u"Mission completion"};
    for(unsigned i=0;i<12;++i){auto& p=s.statistics.entries[i];p.key=600+i;p.label=Label(s,stats[i]);p.scope=bridge::StatisticScope::CurrentMission;p.value={float(i),true};p.maximum={25,true};}
    s.statistics.domain={bridge::Availability::Available,1,12,12};s.chaos={bridge::ChaosLevel::Low,true};
    s.map.domain={bridge::Availability::Available,1,1,1};s.map.key=1;s.map.acquired={true,true};s.map.label=Label(s,u"Distillery District",2009);s.map.sheets.domain={bridge::Availability::Available,1,1,1};s.map.sheets.entries[0]={1,s.map.label.title};
    Ui ui(bridge);if(!ui.Create(argv[1])){fwprintf(stderr,L"%ls\n",ui.Failure().c_str());return 1;}ui.Update(s,1000);
    for(unsigned page=0;page<7;++page){UiTestAccess::Page(ui,bridge::Page(page));ui.Render(1000);UiTestAccess::Bounds(ui);UiTestAccess::Footer(ui);Save(ui,std::filesystem::path(argv[2])/(std::to_wstring(page)+L"-full.bmp"));UiTestAccess::Select(ui);ui.Render(1000);UiTestAccess::Bounds(ui);UiTestAccess::Footer(ui);Save(ui,std::filesystem::path(argv[2])/(std::to_wstring(page)+L"-details.bmp"));UiTestAccess::Scroll(ui);ui.Render(1000);UiTestAccess::Bounds(ui);UiTestAccess::Footer(ui);Save(ui,std::filesystem::path(argv[2])/(std::to_wstring(page)+L"-scroll.bmp"));}
    puts("PASS: seven pages, selected details and partial scrolling rendered at 1240x1080");
    UiTestAccess::Responsiveness(ui,s);
    UiTestAccess::FullStatistics(ui,s);
    UiTestAccess::KeyRingReader(ui,s,bridge);
    UiTestAccess::Interaction(ui,s,bridge);
    UiTestAccess::Lifecycle(ui,s);
    UiTestAccess::InventoryAndPause(ui,s,bridge);
    UiTestAccess::MapBrowsing(ui,s,bridge);
    // Visual fixture at the first independently measured Kingsparrow board.
    s.map.key=5;s.map.label.title=Text(s,u"Kingsparrow Island");s.map.label.artwork.id=2005;
    s.map.playerX={689.5f/800,true};s.map.playerY={297.5f/500,true};
    s.map.facingRadians={-1.55925f,true};s.map.positionApproximate=true;
    ++s.map.domain.revision;++s.sequence;s.sampledAt=5000;
    UiTestAccess::Page(ui,bridge::Page::Map);ui.Update(s,5000);ui.Render(5000);
    UiTestAccess::Bounds(ui);UiTestAccess::Footer(ui);
    Save(ui,std::filesystem::path(argv[2])/L"map-calibrated.bmp");
    s.map.key=4;s.map.label.title=Text(s,u"The Golden Cat");s.map.label.artwork.id=2004;
    const auto pose=ProjectAcquiredMap(4,36389,8847,0,true,4550);
    s.map.playerX={pose.x,pose.positionKnown};s.map.playerY={pose.y,pose.positionKnown};
    s.map.facingRadians={pose.facing,pose.facingKnown};
    s.objectiveTargets.domain={bridge::Availability::Available,1,2,2};
    for(unsigned i=0;i<2;++i) {
        auto& target=s.objectiveTargets.entries[i];target={};target.task=900+i;target.observedAt=6000;
        target.label=Label(s,i?u"Optional objective":u"Current objective");target.optional=i!=0;
        target.worldX={37988,true};target.worldY={15039,true};target.worldZ={i?5350.0f:4420.0f,true};
    }
    ++s.map.domain.revision;++s.sequence;s.sampledAt=6000;
    ui.Update(s,6000);ui.Render(6000);UiTestAccess::Bounds(ui);UiTestAccess::Footer(ui);
    Save(ui,std::filesystem::path(argv[2])/L"map-golden-cat.bmp");
    UiTestAccess::MapTargets(ui,s,bridge,6000);
    s.map.key=9;s.map.label.title=Text(s,u"Distillery District");s.map.label.artwork.id=2009;
    const auto streetPose=ProjectAcquiredMap(9,426,-8000,0.7,true,3100);
    s.map.playerX={streetPose.x,streetPose.positionKnown};s.map.playerY={streetPose.y,streetPose.positionKnown};
    s.map.facingRadians={streetPose.facing,streetPose.facingKnown};
    s.objectiveTargets={};++s.map.domain.revision;++s.sequence;s.sampledAt=7000;
    ui.Update(s,7000);ui.Render(7000);UiTestAccess::Bounds(ui);UiTestAccess::Footer(ui);
    Save(ui,std::filesystem::path(argv[2])/L"map-distillery.bmp");

    const uint8_t maps[]={1,2,3,6};
    const char16_t* names[]={u"Boyle Estate",u"Kaldwin's Bridge",u"The Golden Cat Exterior",u"Office of the High Overseer"};
    const double positions[][3]={{-3078,-1915,200},{9156.6162109375,-3393.415283203125,519},
        {42361.59375,8190.96728515625,3242.38916015625},{2625.33984375,-16037.685546875,-365.8197021484375}};
    for(unsigned i=0;i<4;++i) {
        s.map.key=maps[i];s.map.label.title=Text(s,names[i]);s.map.label.artwork.id=2000+maps[i];
        const auto p=ProjectAcquiredMap(maps[i],positions[i][0],positions[i][1],.7,true,positions[i][2]);
        assert(p.positionKnown && p.facingKnown);
        s.map.playerX={p.x,true};s.map.playerY={p.y,true};s.map.facingRadians={p.facing,true};
        s.map.positionApproximate=true;s.objectiveTargets={};++s.map.domain.revision;++s.sequence;
        s.sampledAt=8000+i;ui.Update(s,8000+i);ui.Render(8000+i);
        UiTestAccess::Bounds(ui);UiTestAccess::Footer(ui);
        Save(ui,std::filesystem::path(argv[2])/(L"map-exterior-"+std::to_wstring(maps[i])+L".bmp"));
    }
    puts("PASS: all four remaining map drawings rendered with calibrated pose at 1240x1080");
    return 0;
}

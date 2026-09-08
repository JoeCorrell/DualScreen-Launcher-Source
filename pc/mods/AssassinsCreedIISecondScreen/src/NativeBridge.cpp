#include <windows.h>
#include <atomic>
#include <array>
#include "NativeBridge.h"
#include "NativeRead.h"
#include "NativeInventoryView.h"
#include "NativeLocalization.h"
#include "NativeDna.h"
#include "NativeDatabase.h"
#include "NativeEquipment.h"
#include "NativeWeaponStats.h"
#include "NativeMap.h"
#include "NativeMapMarkers.h"
#include "NativeMapLegend.h"
#include "NativeSynchronization.h"
#include "NativeVilla.h"
#include "NativeCapes.h"
#include "NativeGlyphs.h"
#include "NativeMission.h"
#include "NativeCapeCalls.h"
#include "NativeHealth.h"
#include "NativeNotoriety.h"
#include "NativeMedicineCalls.h"
#include "NativeWeaponSelection.h"
#include "NativeCollectibles.h"
#include "NativeSampling.h"
#include "NativeWorkGate.h"

namespace ac2ds {
namespace {
class LocalMemory final : public native::Memory {
public:
    bool Copy(uint32_t address, void* output, size_t bytes) const override {
        SIZE_T got{};
        return bytes <= 65536 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), output, bytes, &got) && got == bytes;
    }
};
using Peek = BOOL (WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
Peek original{};
StateStore* destination{};
NativeLogger logNative{};
MapSource* mapImages{};
uint32_t lastMapArtwork{};
uint64_t lastMarkerSample{};
uint32_t markerArtwork{},lastMarkerCount{UINT32_MAX};
std::vector<Marker> lastMarkers;
std::vector<MapCategory> lastMapCategories;
bool lastMarkersReady{};
std::atomic<DWORD> ownerThread{};
thread_local bool servicing{};
native::WorkGate workGate;
HANDLE sampleEvent{};
std::atomic<bool> stopWorker{};
uint64_t lastSample{}, generation{1}, revision{1};
unsigned sampleSlot{};
uint64_t sampleTimingStart{},sampleTimingCount{},sampleTimingTotal{},sampleTimingMaximum{};
native::Roots previous{};
bool hadRoots{};
bool capeCallsReady{};
bool medicineCallsReady{};
native::MedicineAction medicineAction;
bool weaponSelectionCallsReady{};
native::WeaponSelectionAction weaponSelectionAction;
uint64_t actionStatusUntil{};
std::wstring actionStatus;
native::LocalizationCache localization;
bool lastInventoryReady{};
uint32_t lastInventoryCount{UINT32_MAX};
uint32_t lastMemoryCount{UINT32_MAX};
uint64_t lastDnaSample{};
bool dnaReady{};
std::vector<ac2ds::Memory> lastMemories;
uint64_t lastMissionSample{};
std::optional<ac2ds::Memory> lastCurrentMemory;
uint64_t lastMissionIdentity{};
uint32_t lastObjectiveText{UINT32_MAX};
int lastMissionReader{-1};
uint64_t lastDatabaseSample{};
bool databaseReady{};
uint32_t lastDatabaseCount{UINT32_MAX};
std::vector<DatabaseEntry> lastDatabase;
uint64_t lastEquipmentSample{};
bool equipmentReady{};
uint32_t lastEquipmentCount{UINT32_MAX};
std::vector<Item> lastEquipment;
uint64_t lastProgressSample{};
std::optional<native::Synchronization> lastSynchronization;
std::vector<Statistic> lastStatistics;
uint32_t lastSyncEarned{UINT32_MAX},lastSyncMaximum{UINT32_MAX};
std::optional<native::HealthState> lastHealth;
std::optional<double> lastNotoriety;
uint32_t lastHealthCurrent{UINT32_MAX},lastHealthMaximum{UINT32_MAX};
std::array<uint32_t,2> lastAdditionalCompleted{{UINT32_MAX,UINT32_MAX}};
std::array<uint32_t,2> lastAdditionalTotal{{UINT32_MAX,UINT32_MAX}};

void Sample() {
    const auto now = GetTickCount64();
    const unsigned slot=sampleSlot++%4;
    struct Timing {
        LARGE_INTEGER begin{};
        uint64_t now;
        explicit Timing(uint64_t tick):now(tick) {QueryPerformanceCounter(&begin);}
        ~Timing() {
            LARGE_INTEGER end{},frequency{};QueryPerformanceCounter(&end);QueryPerformanceFrequency(&frequency);
            if(frequency.QuadPart<=0)return;
            uint64_t micros=uint64_t(end.QuadPart-begin.QuadPart)*1000000/uint64_t(frequency.QuadPart);
            ++sampleTimingCount;sampleTimingTotal+=micros;sampleTimingMaximum=std::max(sampleTimingMaximum,micros);
            if(!sampleTimingStart)sampleTimingStart=now;
            if(now-sampleTimingStart>=10000) {
                logNative("native worker sample timing count=%llu meanUs=%llu maxUs=%llu refreshSlots=4",
                    static_cast<unsigned long long>(sampleTimingCount),
                    static_cast<unsigned long long>(sampleTimingTotal/sampleTimingCount),
                    static_cast<unsigned long long>(sampleTimingMaximum));
                sampleTimingCount=sampleTimingTotal=sampleTimingMaximum=0;sampleTimingStart=now;
            }
        }
    } timing(now);
    LocalMemory memory; native::Roots roots{};
    bool valid = native::ReadRoots(memory, roots);
    if (!valid) roots = {};
    if (!hadRoots || !(roots == previous)) {
        ++generation; previous = roots; hadRoots = true;
        lastDnaSample = 0; dnaReady = false; lastMemories.clear();
        lastMissionSample=0;lastCurrentMemory.reset();lastMissionReader=-1;
        lastDatabaseSample = 0; databaseReady = false; lastDatabase.clear();
        lastEquipmentSample = 0; equipmentReady = false; lastEquipment.clear();
        lastMarkerSample=0;markerArtwork=0;lastMarkers.clear();
        lastMapCategories.clear();lastMarkersReady=false;
        lastProgressSample=0;lastSynchronization.reset();lastStatistics.clear();
        lastHealth.reset();
        lastNotoriety.reset();
        actionStatus.clear();actionStatusUntil=0;
        logNative("native generation=%llu valid=%d world=%08lx player=%08lx progress=%08lx",
            static_cast<unsigned long long>(generation), int(valid), static_cast<unsigned long>(roots.world),
            static_cast<unsigned long>(roots.player), static_cast<unsigned long>(roots.progress));
    }
    Snapshot snapshot; snapshot.generation = generation; snapshot.revision = ++revision;
    snapshot.sampledAt = now; snapshot.worldIdentity = roots.world; snapshot.playerIdentity = roots.player;
    snapshot.phase = Phase::Loading;
    snapshot.status = L"Waiting for a loaded memory";
    if (valid && roots.world && roots.player && roots.progress) {
        auto inventory = native::ReadPlayerInventory(memory, roots.player);
        auto definitions = native::ReadReloadDefinitions(memory);
        bool textReady = localization.Refresh(memory);
        if(mapImages){
            auto map=native::ReadMap(memory,roots);
            if(map){
                if(textReady && (native::RefreshDomain(lastMarkerSample,now,slot,0) || markerArtwork!=map->artwork)){
                    lastMarkerSample=now;markerArtwork=map->artwork;lastMarkers.clear();
                    lastMarkersReady=false;
                    lastMapCategories=native::MapCategoryCatalog([](uint32_t id){return localization.Lookup(id);});
                    auto markers=native::ReadMapMarkers(memory,roots,*map);
                    if(markers){
                        for(const auto& source:*markers){
                            Marker marker;marker.id=source.id;marker.x=source.x;marker.y=source.y;
                            auto title=localization.Lookup(native::MapMarkerTitle(source.type));
                            if(title)marker.name=*title;
                            marker.visible=true;marker.discovered=true;marker.artworkId=native::MapMarkerArtwork(source.type);
                            marker.categoryId=native::MapCategoryId(source.type);
                            lastMarkers.push_back(std::move(marker));
                        }
                        lastMarkersReady=true;
                    }
                    if(!localization.StillCurrent(memory)){lastMarkers.clear();lastMapCategories.clear();lastMarkersReady=false;}
                    if(lastMarkerCount!=lastMarkers.size()){
                        logNative("native map markers visible=%u reader=%d; native class visibility rules checked",
                            unsigned(lastMarkers.size()),int(markers.has_value()));lastMarkerCount=unsigned(lastMarkers.size());
                    }
                }
                MapSurface surface{map->artwork,map->maskWidth,map->maskHeight,std::move(map->discovery)};
                snapshot.map=mapImages->Request(generation,surface);
                snapshot.mapX=map->x;snapshot.mapY=map->y;snapshot.heading=map->heading;snapshot.capabilities|=Player;
                if(textReady)if(auto title=localization.Lookup(map->title))snapshot.location=*title;
                if(snapshot.map){snapshot.mapVisibilityVerified=true;snapshot.capabilities|=Map;
                    if(textReady && markerArtwork==map->artwork){snapshot.markers=lastMarkers;
                        snapshot.mapCategories=lastMapCategories;snapshot.mapMarkersVerified=lastMarkersReady;}}
                if(lastMapArtwork!=map->artwork){
                    logNative("native map artwork=%08lx mask=%lux%lu; discovery shadow and projection verified",
                        static_cast<unsigned long>(map->artwork),static_cast<unsigned long>(map->maskWidth),static_cast<unsigned long>(map->maskHeight));
                    lastMapArtwork=map->artwork;
                }
            }else{lastMarkers.clear();lastMapCategories.clear();lastMarkersReady=false;lastMarkerSample=0;}
        }
        const bool inventoryReady = inventory && definitions && textReady;
        if (inventory) {
            for (const auto& container : inventory->containers) if (container.category == 1) {
                snapshot.money = container.quantity; snapshot.capabilities |= Player;
            }
        }
        if (inventoryReady) {
            snapshot.items = native::InventoryItems(*inventory, *definitions, revision,
                [](uint32_t id) { return localization.Lookup(id); });
            if (localization.StillCurrent(memory)) snapshot.capabilities |= Inventory;
            else { snapshot.items.clear(); localization.Clear(); textReady = false; }
        }
        if(medicineCallsReady && (snapshot.capabilities&Inventory) && native::ResolveMedicineTarget(memory,roots))
            for(auto& item:snapshot.items)if(item.id==14)item.actions=ActionBit(ActionKind::Use);
        if (textReady && native::RefreshDomain(lastEquipmentSample,now,slot,0)) {
            lastEquipmentSample=now;equipmentReady=false;lastEquipment.clear();
            auto gear=native::ReadEquipment(memory,roots.player);
            auto capes=native::ReadCapes(memory,roots.player);
            const bool gearReady=gear.has_value();
            uint32_t wornCape{};if(capes)for(const auto& cape:*capes)if(cape.equipped)wornCape=cape.menu;
            uint32_t ui{},uiType{};uint8_t capeRestricted=1;
            bool capeSelectionReady=capeCallsReady && wornCape && wornCape!=26 && memory.Read(0x223f820,ui) &&
                memory.Read(ui,uiType) && uiType==0x1e60898 && memory.Member(ui,0xfe,capeRestricted) && !capeRestricted;
            if(capes){if(!gear)gear=std::vector<native::EquipmentRecord>{};gear->insert(gear->end(),capes->begin(),capes->end());}
            if(gear){
                for(const auto& record:*gear){
                    auto name=localization.Lookup(record.title);if(!name || name->empty())continue;
                    Item item;item.id=(uint64_t(1)<<32)|record.id;item.revision=revision;item.name=*name;
                    if(auto description=localization.Lookup(record.description))item.description=*description;
                    constexpr const wchar_t* categories[]{L"Chest armor",L"Vambraces",L"Shoulders",L"Greaves",
                        L"Secondary weapon",L"Primary weapon",L"Hidden blade",L"Double blade",L"Pistol"};
                    const bool cape=record.menu==22 || record.menu==23 || record.menu==25 || record.menu==26 || record.menu==33;
                    item.category=cape?L"Cape":record.menu<9?categories[record.menu]:L"Hidden blade upgrades";
                    item.owned=true;item.equipped=record.equipped;item.artworkId=record.artworkId;item.thumbnailId=record.thumbnailId;
                    item.details=record.equipped?L"Equipped":cape?L"Unlocked":L"Carried";
                    if(auto stats=native::ReadWeaponStats(memory,roots,record))
                        item.weaponRatings=std::array<unsigned,3>{stats->damage,stats->speed,stats->deflection};
                    if(cape && capeSelectionReady && !record.equipped && record.menu!=26)item.actions=ActionBit(ActionKind::Equip);
                    if(weaponSelectionCallsReady && (record.menu>=4 && record.menu<=7) &&
                        native::ResolveWeaponSelectionTarget(memory,roots,record.id))item.actions=ActionBit(ActionKind::Select);
                    lastEquipment.push_back(std::move(item));
                }
                equipmentReady=localization.StillCurrent(memory);if(!equipmentReady)lastEquipment.clear();
            }
            if(lastEquipmentCount!=lastEquipment.size()){
                logNative("native equipment visible=%u carriedReader=%d capeReader=%d; cape event gate=%d",
                    unsigned(lastEquipment.size()),int(gearReady),int(capes.has_value()),int(capeSelectionReady));lastEquipmentCount=unsigned(lastEquipment.size());
            }
        }
        if(textReady && equipmentReady){
            snapshot.items.insert(snapshot.items.begin(),lastEquipment.begin(),lastEquipment.end());
            for(auto& item:snapshot.items)item.revision=revision;
            snapshot.capabilities|=Inventory|Equipment;
        }else if(!textReady){equipmentReady=false;lastEquipment.clear();lastEquipmentSample=0;}
        if (textReady && native::RefreshDomain(lastDnaSample,now,slot,1)) {
            lastDnaSample = now;
            lastMemories.clear(); dnaReady = false;
            auto dna = native::ReadDna(memory);
            if (dna) {
                for (const auto& entry : *dna) {
                    ac2ds::Memory item;
                    item.id = (uint64_t(entry.sequence + 1) << 32) | (entry.index + 1);
                    item.revision = revision;
                    auto sequenceTitle = localization.Lookup(entry.sequenceTitle);
                    item.sequence = sequenceTitle ? *sequenceTitle : L"";
                    auto title = entry.title ? localization.Lookup(entry.title) : std::optional<std::wstring>{};
                    auto description = entry.description ? localization.Lookup(entry.description) : std::optional<std::wstring>{};
                    if (entry.title && !title) continue;
                    item.name = title ? *title : L"MEMORY " + std::to_wstring(entry.index + 1);
                    if (description) item.description = *description;
                    else if (entry.state == 1) {
                        // Original MemoryDNA uses this text for an unlocked
                        // memory whose identity has not yet been discovered.
                        auto unlocated = localization.Lookup(286291);
                        if (unlocated) item.description = *unlocated;
                    }
                    item.unlocked = entry.state > 0; item.exposed = true;
                    item.complete = entry.state == 3; item.active = entry.active;
                    item.synchronization = entry.synchronization;
                    lastMemories.push_back(std::move(item));
                }
                dnaReady = localization.StillCurrent(memory);
                if (!dnaReady) lastMemories.clear();
            }
            if (lastMemories.size() != lastMemoryCount) {
                logNative("native DNA visible=%u reader=%d; conditional and missing-DLC branches deferred; replay disabled",
                    unsigned(lastMemories.size()), int(dna.has_value()));
                lastMemoryCount = unsigned(lastMemories.size());
            }
        }
        if (textReady && dnaReady) {
            snapshot.memories = lastMemories; snapshot.capabilities |= Missions;
            for (auto& item : snapshot.memories) item.revision = revision;
        } else if (!textReady) { lastMemories.clear(); dnaReady = false; lastDnaSample = 0; }
        if(textReady && native::RefreshDomain(lastMissionSample,now,slot,1)){
            lastMissionSample=now;lastCurrentMemory.reset();
            auto mission=native::ReadCurrentMission(memory,roots);
            if(mission && mission->present){
                auto title=localization.Lookup(mission->title);
                if(title){
                    ac2ds::Memory item;item.id=mission->Identity();item.name=*title;
                    item.unlocked=true;item.exposed=true;item.active=true;item.synchronization=mission->synchronization;
                    if(mission->side)item.sequence=L"Side activity";
                    else if(auto sequence=localization.Lookup(mission->sequenceTitle))item.sequence=*sequence;
                    if(mission->description){if(auto description=localization.Lookup(mission->description))item.description=*description;}
                    if(mission->objective){if(auto objective=localization.Lookup(*mission->objective))item.objective=*objective;}
                    if(localization.StillCurrent(memory))lastCurrentMemory=std::move(item);
                }
            }
            const auto identity=mission && mission->present?mission->Identity():0;
            const auto objective=mission && mission->objective?*mission->objective:UINT32_MAX;
            if(int(mission.has_value())!=lastMissionReader || identity!=lastMissionIdentity || objective!=lastObjectiveText){
                logNative("native current mission reader=%d identity=%llu objective=%u",int(mission.has_value()),
                    static_cast<unsigned long long>(identity),objective);
                lastMissionIdentity=identity;lastObjectiveText=objective;lastMissionReader=int(mission.has_value());
            }
        }else if(!textReady){lastMissionSample=0;lastCurrentMemory.reset();}
        if(lastCurrentMemory){
            auto current=*lastCurrentMemory;current.revision=revision;
            snapshot.objective=current.objective;snapshot.capabilities|=Missions;
            bool found=false;
            for(auto& item:snapshot.memories){
                item.active=item.id==current.id;
                if(item.active){item= current;found=true;}
            }
            if(!found)snapshot.memories.push_back(std::move(current));
        }
        if (textReady && native::RefreshDomain(lastDatabaseSample,now,slot,2)) {
            lastDatabaseSample = now; lastDatabase.clear(); databaseReady = false;
            auto entries = native::ReadDatabase(memory, roots.progress);
            if (entries) {
                for (const auto& entry : *entries) {
                    auto title = localization.Lookup(entry.title);
                    auto description = localization.Lookup(entry.description);
                    uint32_t categoryId = entry.category == 1 ? 282251 : entry.category == 2 ? 282252 :
                        entry.category == 7 ? 282281 : 282283;
                    auto category = localization.Lookup(categoryId);
                    if (!title || !description || !category) continue;
                    DatabaseEntry item; item.id = entry.id; item.category = *category;
                    item.title = *title; item.text = *description; item.unlocked = true;
                    item.artworkId = entry.artworkId;
                    lastDatabase.push_back(std::move(item));
                }
                std::stable_sort(lastDatabase.begin(), lastDatabase.end(), [](const DatabaseEntry& a, const DatabaseEntry& b) {
                    return a.category != b.category ? a.category < b.category : a.title < b.title;
                });
                databaseReady = localization.StillCurrent(memory);
                if (!databaseReady) lastDatabase.clear();
            }
            if (lastDatabase.size() != lastDatabaseCount) {
                logNative("native database visible=%u reader=%d; discovery and Codex state verified",
                    unsigned(lastDatabase.size()), int(entries.has_value()));
                lastDatabaseCount = unsigned(lastDatabase.size());
            }
        }
        if (textReady && databaseReady) { snapshot.database = lastDatabase; snapshot.capabilities |= Database; }
        else if (!textReady) { lastDatabase.clear(); databaseReady = false; lastDatabaseSample = 0; }
        const bool ready = (snapshot.capabilities & Inventory) != 0;
        if (ready != lastInventoryReady || snapshot.items.size() != lastInventoryCount) {
            logNative("native inventory ready=%d categories=%u visible=%u text=%d; actions require native eligibility",
                int(ready), inventory ? unsigned(inventory->containers.size()) : 0u,
                unsigned(snapshot.items.size()), int(textReady));
            lastInventoryReady = ready; lastInventoryCount = unsigned(snapshot.items.size());
        }
        if(native::RefreshDomain(lastProgressSample,now,slot,3)){
            lastProgressSample=now;lastStatistics.clear();
            lastHealth=native::ReadHealth(memory,roots);
            auto notoriety=native::ReadNotoriety(memory,roots);
            if(notoriety.has_value()!=lastNotoriety.has_value())
                logNative("native notoriety reader=%d",int(notoriety.has_value()));
            lastNotoriety=notoriety;
            auto healthCurrent=lastHealth?lastHealth->current:UINT32_MAX;
            auto healthMaximum=lastHealth?lastHealth->maximum:UINT32_MAX;
            if(healthCurrent!=lastHealthCurrent || healthMaximum!=lastHealthMaximum){
                logNative("native health reader=%d current=%u maximum=%u",int(lastHealth.has_value()),
                    lastHealth?healthCurrent:0u,lastHealth?healthMaximum:0u);
                lastHealthCurrent=healthCurrent;lastHealthMaximum=healthMaximum;
            }
            lastSynchronization=native::ReadSynchronization(memory,roots);
            const auto earned=lastSynchronization?lastSynchronization->earned:UINT32_MAX;
            const auto maximum=lastSynchronization?lastSynchronization->maximum:UINT32_MAX;
            if(earned!=lastSyncEarned || maximum!=lastSyncMaximum){
                logNative("native synchronization ready=%d earned=%u maximum=%u",int(lastSynchronization.has_value()),
                    lastSynchronization?earned:0u,lastSynchronization?maximum:0u);
                lastSyncEarned=earned;lastSyncMaximum=maximum;
            }
            if(lastSynchronization){
                const auto& sync=*lastSynchronization;
                lastStatistics.push_back({L"Story memories synchronized",double(sync.story.completed),double(sync.story.total),false});
                lastStatistics.push_back({L"Sequences synchronized",double(sync.sequences.completed),double(sync.sequences.total),false});
                // These are aggregate native values only. No locked memory
                // names, objectives or locations are exposed by this dashboard.
                if(textReady)for(size_t i=0;i<sync.side.size();++i){
                    if(!sync.side[i].total)continue;
                    auto title=localization.Lookup(sync.sideTitles[i]);
                    if(title && !title->empty())lastStatistics.push_back({*title,double(sync.side[i].completed),double(sync.side[i].total),false});
                }
            }
            // Aggregate counters expose no locked locations. Page copies keep
            // the collection walk bounded and reject mid-read save changes.
            native::ReadTransaction progressMemory(memory);
            constexpr std::array<std::pair<uint32_t, const wchar_t*>, 4> collections{{
                {0x170, L"Feathers"}, {0x180, L"Codex pages"}, {0x184, L"Statuettes"}, {0x154, L"Assassin seals"}}};
            for(const auto& collection:collections){
                auto value=native::ReadCollection(progressMemory,roots.progress,collection.first);
                if(value)lastStatistics.push_back({collection.second,double(value->completed),double(value->total),false});
            }
            if(!progressMemory.Validate())lastStatistics.clear();
            for(size_t i=0;i<2;++i){
                auto value=native::ReadAdditionalCollection(memory,roots,i?native::CollectibleKind::Treasure:native::CollectibleKind::Viewpoints);
                if(value)lastStatistics.push_back({i?L"Treasure chests":L"Viewpoints synchronized",double(value->completed),double(value->total),false});
                const auto completed=value?value->completed:UINT32_MAX,total=value?value->total:UINT32_MAX;
                if(completed!=lastAdditionalCompleted[i] || total!=lastAdditionalTotal[i]){
                    logNative("native additional collection kind=%u reader=%d completed=%u total=%u",unsigned(i),int(value.has_value()),
                        value?completed:0u,value?total:0u);
                    lastAdditionalCompleted[i]=completed;lastAdditionalTotal[i]=total;
                }
            }
            auto glyphs=native::ReadGlyphs(memory,roots);
            if(glyphs)lastStatistics.push_back({L"Glyph puzzles solved",double(glyphs->completed),double(glyphs->total),false});
            auto villa=native::ReadVilla(memory,roots);
            if(villa && villa->unlocked){
                lastStatistics.push_back({L"Monteriggioni restored",double(villa->restored),100.0,true});
                lastStatistics.push_back({L"Monteriggioni value (florins)",double(villa->value),{},false});
                lastStatistics.push_back({L"Monteriggioni population",double(villa->population),{},false});
            }
        }
        snapshot.statistics=lastStatistics;
        snapshot.notoriety=lastNotoriety;
        if(lastHealth){snapshot.health=lastHealth->current;snapshot.maximumHealth=lastHealth->maximum;snapshot.capabilities|=Player;}
        if(lastSynchronization)snapshot.synchronization=lastSynchronization->Percentage();
        native::Roots after{};
        if (native::ReadRoots(memory, after) && roots == after) {
            snapshot.phase = Phase::Playing;
            snapshot.status = now<actionStatusUntil?actionStatus:L"Memory connected";
            if (!snapshot.statistics.empty() || snapshot.synchronization) snapshot.capabilities |= Progress;
        } else {
            snapshot = Snapshot{}; snapshot.generation = generation; snapshot.revision = revision;
            snapshot.sampledAt = now; snapshot.phase = Phase::Loading;
            snapshot.status = L"Synchronizing memory transition";
        }
    }
    destination->Publish(std::move(snapshot));
}
// Only this function invokes game code. It runs at the verified game-thread
// boundary, and never waits for the reader or the UI snapshot mutex.
void ServiceAction(uint64_t now) {
    auto pending=destination->TryTakeForGame(now);
    if(!pending)return;
    const auto& command=pending->command;
    const auto& displayed=*pending->displayed;
    LocalMemory memory;native::Roots roots{};
    auto result=native::CapeActionResult::Unavailable;
    const bool current=native::ReadRoots(memory,roots) && roots.world && roots.player && roots.progress &&
        hadRoots && roots==previous && command.generation==generation &&
        displayed.playerIdentity==roots.player && displayed.worldIdentity==roots.world;
    if(command.kind==ActionKind::Use && command.entity==14){
        auto medicineResult=native::MedicineActionResult::Unavailable;
        if(current && medicineCallsReady){
            native::MedicineEventEngine engine(reinterpret_cast<native::MedicineRequestCall>(0x10d7830));
            medicineResult=medicineAction.Use(memory,roots,now,engine);
        }
        logNative("native medicine action request=%llu result=%d gameThread=%lu",
            static_cast<unsigned long long>(command.request),int(medicineResult),GetCurrentThreadId());
        actionStatus=medicineResult==native::MedicineActionResult::Dispatched?L"Medicine requested":L"Medicine unavailable";
        actionStatusUntil=now+3000;lastProgressSample=0;lastHealth.reset();lastSample=0;
        return;
    }
    if(command.kind==ActionKind::Select && (command.entity>>32)==1){
        auto bladeResult=native::WeaponSelectionResult::Unavailable;
        if(current && weaponSelectionCallsReady){
            native::WeaponSelectionEventEngine engine(reinterpret_cast<native::PrimaryRequestCall>(0x10d75c0));
            bladeResult=weaponSelectionAction.Select(memory,roots,uint32_t(command.entity),now,engine);
        }
        logNative("native weapon selection request=%llu item=%llu result=%d gameThread=%lu",
            static_cast<unsigned long long>(command.request),static_cast<unsigned long long>(command.entity),
            int(bladeResult),GetCurrentThreadId());
        actionStatus=bladeResult==native::WeaponSelectionResult::Dispatched?L"Weapon selection requested":L"Weapon unavailable";
        actionStatusUntil=now+3000;lastEquipmentSample=0;lastEquipment.clear();equipmentReady=false;lastSample=0;
        return;
    }
    if(current && capeCallsReady &&
        command.kind==ActionKind::Equip && (command.entity>>32)==1){
        native::CapeEventEngine engine(reinterpret_cast<native::CapePermissionCall>(0x10b3750),
            reinterpret_cast<native::CapeMutationCall>(0x10b3c30));
        result=native::EquipCape(memory,roots,uint32_t(command.entity),engine);
    }
    logNative("native cape action request=%llu item=%llu result=%d gameThread=%lu",
        static_cast<unsigned long long>(command.request),static_cast<unsigned long long>(command.entity),
        int(result),GetCurrentThreadId());
    actionStatus=result==native::CapeActionResult::Dispatched?L"Equipment change requested":L"Equipment change unavailable";
    actionStatusUntil=now+3000;
    lastEquipmentSample=0;lastEquipment.clear();equipmentReady=false;
    // The next sample re-resolves roots before touching any cache; callbacks
    // may have changed actors/world. No old snapshot is republished here.
    lastSample=0;
}
DWORD WINAPI ReadWorker(LPVOID) noexcept {
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    for(;;){
        if(WaitForSingleObject(sampleEvent,INFINITE)!=WAIT_OBJECT_0)return 0;
        if(stopWorker.load())return 0;
        if(!workGate.BeginRead())continue;
        struct Release { ~Release(){workGate.Complete();} } release;
        try { Sample(); }
        catch (...) { logNative("native worker sample discarded after internal exception"); }
    }
}
void ServicePump() {
    const auto thread=GetCurrentThreadId();DWORD expected{};
    if(ownerThread.compare_exchange_strong(expected,thread))
        logNative("native message pump resolved thread=%lu; read-only worker, nonblocking action boundary",thread);
    if(ownerThread.load()!=thread || servicing)return;
    servicing=true;struct Reset {~Reset(){servicing=false;}} reset;
    const auto now=GetTickCount64();
    if(workGate.BeginAction()){
        struct Release {~Release(){workGate.Complete();}} release;
        ServiceAction(now);
    }
    // An auto-reset event and gate allow exactly one sample in flight. Missed
    // intervals are dropped, never queued up into a post-stall burst.
    if(now-lastSample>=250 && workGate.Schedule()){
        lastSample=now;
        if(!SetEvent(sampleEvent))workGate.CancelPending();
    }
}
BOOL WINAPI Pump(LPMSG message, HWND window, UINT first, UINT last, UINT flags) noexcept {
    auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    BOOL result = original(message, window, first, last, flags);
    // Only the two verified calls in Scimitar's 00b06a40 message pump qualify.
    // The panel and Wine's own message loops never enter the adapter.
    if (!result && !window && !first && !last && flags == PM_REMOVE &&
        (caller == 0x00b06a5b || caller == 0x00b06a91)) {
        try { ServicePump(); }
        catch (...) { if (logNative) logNative("native action boundary discarded after internal exception"); }
    }
    return result;
}
}
bool InstallNativeBridge(StateStore& store, NativeLogger logger, MapSource* maps) {
    if (destination || !logger || reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != 0x400000) return false;
    LocalMemory memory;
    const unsigned char expected[]{0x55,0x8b,0xec,0x83,0xec,0x1c,0x56,0x8b,0x35,0x2c,0x33,0xca,0x01};
    unsigned char actual[sizeof(expected)]{};
    if (!memory.Copy(0x00b06a40, actual, sizeof(actual)) || std::memcmp(actual, expected, sizeof(actual))) return false;
    const std::array<uint8_t,16> permissionBytes{{0x55,0x8b,0xec,0x6a,0xff,0x68,0x00,0x62,0xbe,0x01,0x64,0xa1,0,0,0,0}};
    auto mutationBytes=permissionBytes;mutationBytes[6]=0x40;std::array<uint8_t,16> functionBytes{};
    capeCallsReady=memory.Read(0x10b3750,functionBytes) && functionBytes==permissionBytes &&
        memory.Read(0x10b3c30,functionBytes) && functionBytes==mutationBytes;
    const std::array<uint8_t,16> medicineBytes{{0x55,0x8b,0xec,0x6a,0xff,0x68,0xd0,0x91,0xbe,0x01,0x64,0xa1,0,0,0,0}};
    const std::array<uint8_t,15> medicineSubscriberBytes{{0x55,0x8b,0xec,0x8b,0x45,0x08,0x89,0x41,0x1c,0x33,0xc0,0x5d,0xc2,0x04,0x00}};
    std::array<uint8_t,15> subscriberBytes{};
    medicineCallsReady=memory.Read(0x10d7830,functionBytes) && functionBytes==medicineBytes &&
        memory.Read(0x15f21b0,subscriberBytes) && subscriberBytes==medicineSubscriberBytes;
    auto primaryBytes=medicineBytes;primaryBytes[6]=0xb0;
    const std::array<uint8_t,18> primarySubscriberBytes{{0x55,0x8b,0xec,0x8b,0x41,0x0c,0x8b,0x4d,0x08,0x89,0x48,0x5c,0x33,0xc0,0x5d,0xc2,0x04,0x00}};
    std::array<uint8_t,18> primaryActual{};
    weaponSelectionCallsReady=memory.Read(0x10d75c0,functionBytes) && functionBytes==primaryBytes &&
        memory.Read(0x15f28a0,primaryActual) && primaryActual==primarySubscriberBytes;
    auto slot = reinterpret_cast<void**>(0x01ca332c);
    void* prior{}; if (!memory.Copy(0x01ca332c, &prior, sizeof(prior))) return false;
    FARPROC exported = GetProcAddress(GetModuleHandleW(L"user32.dll"), "PeekMessageA");
    void* address{}; std::memcpy(&address, &exported, sizeof(address));
    if (!address || address != prior) { logger("native hook refused: PeekMessageA already replaced"); return false; }
    DWORD protection{};
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    std::memcpy(&original, &prior, sizeof(original)); destination = &store; logNative = logger; mapImages=maps;
    stopWorker.store(false);
    sampleEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    HANDLE worker=sampleEvent?CreateThread(nullptr,0,ReadWorker,nullptr,0,nullptr):nullptr;
    if(!worker){
        if(sampleEvent){CloseHandle(sampleEvent);sampleEvent=nullptr;}
        DWORD ignored{};VirtualProtect(slot,sizeof(void*),protection,&ignored);
        destination=nullptr;logger("native hook refused: state worker initialization failed");return false;
    }
    void* replacement{}; Peek hook = Pump; std::memcpy(&replacement, &hook, sizeof(replacement));
    bool installed = InterlockedCompareExchangePointer(slot, replacement, prior) == prior;
    DWORD ignored{}; VirtualProtect(slot, sizeof(void*), protection, &ignored);
    if (!installed) {
        stopWorker.store(true);SetEvent(sampleEvent);WaitForSingleObject(worker,INFINITE);
        CloseHandle(sampleEvent);sampleEvent=nullptr;destination=nullptr;
        logger("native hook refused: import changed during installation");
    } else logger("native hook installed IAT=01ca332c pump=00b06a40 workerInterval=250ms; cape events verified=%d medicine event verified=%d weapon selection event verified=%d",
        int(capeCallsReady),int(medicineCallsReady),int(weaponSelectionCallsReady));
    // The proxy, map caches and worker are pinned for process lifetime.
    CloseHandle(worker);
    return installed;
}
}

// Private research helper. Built with -mwindows; never opens/activates a window,
// calls game functions, acquires controllers, launches AC II, or writes its memory.
// Output is a bounded snapshot for tracing the game's native inventory services.
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <cstdio>
#include <map>
#include "../src/NativeRead.h"
#include "../src/NativeInventory.h"
#include "../src/NativeInventoryMenu.h"
#include "../src/NativeLocalization.h"
#include "../src/NativeDna.h"
#include "../src/NativeDatabase.h"
#include "../src/NativeEquipment.h"
#include "../src/NativeWeaponStats.h"
#include "../src/NativeMap.h"
#include "../src/NativeMapMarkers.h"
#include "../src/NativeSynchronization.h"
#include "../src/NativeVilla.h"
#include "../src/NativeCapes.h"
#include "../src/NativeGlyphs.h"
#include "../src/NativeMission.h"
#include "../src/NativeHealth.h"
#include "../src/NativeNotoriety.h"
#include "../src/NativeMedicineAction.h"
#include "../src/NativeWeaponSelection.h"
#include "../src/NativeCollectibles.h"
#include "../src/NativeCharacterSource.h"
#include "../src/NativeCharacterGeometry.h"
#include "../src/NativeCharacterPalette.h"

using namespace ac2ds::native;
struct Remote final : Memory {
    HANDLE process;
    explicit Remote(HANDLE handle) : process(handle) {}
    bool Copy(uint32_t address, void* output, size_t bytes) const override {
        if (address < 0x10000 || bytes > 65536 || address > UINT32_MAX - bytes) return false;
        SIZE_T got{};
        return ReadProcessMemory(process, reinterpret_cast<void*>(address), output, bytes, &got) && got == bytes;
    }
};
static void Words(FILE* out, const Remote& memory, const char* label, uint32_t address, size_t count) {
    std::array<uint32_t, 384> words{};
    if (count > words.size() || !memory.Copy(address, words.data(), count * 4)) {
        std::fprintf(out, "%s %08lx unreadable\n", label, static_cast<unsigned long>(address)); return;
    }
    std::fprintf(out, "%s %08lx\n", label, static_cast<unsigned long>(address));
    for (size_t i = 0; i < count; ++i)
        std::fprintf(out, "%03x=%08lx%c", unsigned(i * 4), static_cast<unsigned long>(words[i]),
            (i + 1) % 8 == 0 || i + 1 == count ? '\n' : ' ');
}
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    FILE* out = std::fopen("F:\\Download\\ac2-inventory-probe.txt", "wb");
    if (!out) return 1;
    struct Report { FILE* file; ~Report() { std::fputs("\nprobe-complete\n",file);std::fclose(file); } } report{out};
    DWORD target{};
    HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (list == INVALID_HANDLE_VALUE) return 2;
    PROCESSENTRY32 entry{}; entry.dwSize = sizeof(entry);
    bool ambiguous = false;
    if (Process32First(list, &entry)) do {
        if (!_stricmp(entry.szExeFile, "AssassinsCreedIIGame.exe")) {
            if (target) ambiguous = true;
            target = entry.th32ProcessID;
        }
    } while (Process32Next(list, &entry));
    CloseHandle(list);
    if (!target || ambiguous) { std::fprintf(out, "No unique running AC II process\n"); return 3; }
    HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, target);
    if (!process) { std::fprintf(out, "OpenProcess error=%lu\n", GetLastError()); return 4; }
    struct Process { HANDLE handle; ~Process() { CloseHandle(handle); } } cleanup{process};
    Remote memory(process);
    DWORD_PTR processMask{},systemMask{};SYSTEM_INFO cpu{};GetSystemInfo(&cpu);
    if(GetProcessAffinityMask(process,&processMask,&systemMask))
        std::fprintf(out,"native-cpu processors=%lu processMask=%08lx systemMask=%08lx activeMask=%08lx\n",
            cpu.dwNumberOfProcessors,static_cast<unsigned long>(processMask),static_cast<unsigned long>(systemMask),static_cast<unsigned long>(cpu.dwActiveProcessorMask));
    using ThreadQuery=LONG(WINAPI*)(HANDLE,ULONG,void*,ULONG,ULONG*);
    auto proc=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread");ThreadQuery query{};
    std::memcpy(&query,&proc,sizeof(query));
    HANDLE threads=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);THREADENTRY32 thread{};thread.dwSize=sizeof(thread);
    if(threads!=INVALID_HANDLE_VALUE){
        if(Thread32First(threads,&thread))do{if(thread.th32OwnerProcessID!=target)continue;
            HANDLE handle=OpenThread(THREAD_QUERY_INFORMATION,FALSE,thread.th32ThreadID);if(!handle)continue;
            void* start{};if(query)query(handle,9,&start,sizeof(start),nullptr);
            FILETIME created{},exited{},kernel{},user{};
            if(GetThreadTimes(handle,&created,&exited,&kernel,&user)){
                auto ticks=[](FILETIME value){return (uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;};
                std::fprintf(out,"native-thread tid=%lu start=%08lx cpu100ns=%llu\n",thread.th32ThreadID,
                    static_cast<unsigned long>(reinterpret_cast<uintptr_t>(start)),static_cast<unsigned long long>(ticks(kernel)+ticks(user)));
            }
            CloseHandle(handle);
        }while(Thread32Next(threads,&thread));
        CloseHandle(threads);
    }
    HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,target);MODULEENTRY32 module{};module.dwSize=sizeof(module);
    if(modules!=INVALID_HANDLE_VALUE){
        if(Module32First(modules,&module))do{
            if(!_stricmp(module.szModule,"dinput8.dll") || !_stricmp(module.szModule,"AssassinsCreedIIGame.exe"))
                std::fprintf(out,"native-module name=%s base=%08lx size=%lu\n",module.szModule,
                    static_cast<unsigned long>(reinterpret_cast<uintptr_t>(module.modBaseAddr)),module.modBaseSize);
        }while(Module32Next(modules,&module));
        CloseHandle(modules);
    }
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 pe{};
    // This helper performs no hook activation. The payload separately gates all
    // activation with the complete on-disk executable SHA-256.
    if (!memory.Read(0x400000, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0 || dos.e_lfanew > 4096 || !memory.Read(0x400000 + dos.e_lfanew, pe) ||
        pe.Signature != IMAGE_NT_SIGNATURE || pe.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe.FileHeader.TimeDateStamp != 0x4b8c2c75 || pe.OptionalHeader.SizeOfImage != 0x216e000) {
        std::fprintf(out, "Unsupported PE; no game data read\n"); return 5;
    }
    Roots roots{};
    if (!ReadRoots(memory, roots) || !roots.player) {
        std::fprintf(out, "No loaded player; no inventory inspection\n"); return 6;
    }
    std::fprintf(out, "pid=%lu world=%08lx player=%08lx progress=%08lx\n", target,
        static_cast<unsigned long>(roots.world), static_cast<unsigned long>(roots.player),
        static_cast<unsigned long>(roots.progress));
    auto owned = ReadPlayerInventory(memory, roots.player);
    for(auto kind:{CollectibleKind::Viewpoints,CollectibleKind::Treasure}){
        auto collection=ReadAdditionalCollection(memory,roots,kind);
        std::fprintf(out,"guarded-additional kind=%d ready=%d completed=%u total=%u\n",int(kind),int(collection.has_value()),
            collection?collection->completed:0u,collection?collection->total:0u);
    }
    uint32_t collectedOwner{};
    if(memory.Read(0x223c590,collectedOwner) && collectedOwner){
        Words(out,memory,"collected-inventory-owner",collectedOwner,24);
        PointerArray collected{};
        if(ArrayHeader(memory,collectedOwner,0x30,4096,collected)){
            std::fprintf(out,"collected-inventory-count=%u\n",collected.Count());
            for(uint32_t i=0;i<collected.Count() && i<3;++i){uint32_t h{},item{};
                if(memory.Field(collected.data,i*4,h) && memory.Reference(h,item) && item)
                    Words(out,memory,"collected-inventory-item",item,12);
            }
        }
    }
    auto health=ReadHealth(memory,roots);
    auto notoriety=ReadNotoriety(memory,roots);
    std::fprintf(out,"guarded-notoriety=%d percent=%.4f\n",int(notoriety.has_value()),notoriety.value_or(-1));
    std::fprintf(out,"guarded-health=%d current=%u maximum=%u unavailable=%u\n",int(health.has_value()),
        health?health->current:0u,health?health->maximum:0u,health?health->unavailable:0u);
    auto medicine=ResolveMedicineTarget(memory,roots);
    std::fprintf(out,"guarded-medicine-request-eligible=%d; read-only, no action sent\n",int(medicine.has_value()));
    auto mission=ReadCurrentMission(memory,roots);
    std::fprintf(out,"guarded-current-mission=%d present=%d side=%d identity=%llu title=%u description=%u objective=%u\n",
        int(mission.has_value()),mission?int(mission->present):0,mission?int(mission->side):0,
        mission && mission->present?static_cast<unsigned long long>(mission->Identity()):0ull,
        mission?mission->title:0u,mission?mission->description:0u,mission && mission->objective?*mission->objective:UINT32_MAX);
    uint32_t objectiveUi{},objectiveRecord{};
    if(memory.Read(0x223f820,objectiveUi) && memory.Member(objectiveUi,0x110,objectiveRecord) && objectiveRecord)
        Words(out,memory,"current-objective-record",objectiveRecord,13);
    auto glyphs=ReadGlyphs(memory,roots);
    std::fprintf(out,"guarded-glyphs=%d solved=%u total=%u\n",int(glyphs.has_value()),glyphs?glyphs->completed:0u,glyphs?glyphs->total:0u);
    auto capes=ReadCapes(memory,roots.player);
    std::fprintf(out,"guarded-capes=%d count=%u\n",int(capes.has_value()),capes?unsigned(capes->size()):0u);
    if(capes)for(const auto& cape:*capes)std::fprintf(out,"cape menu=%u id=%08x equipped=%d\n",cape.menu,cape.id,int(cape.equipped));
    auto synch=ReadSynchronization(memory,roots);
    auto villa=ReadVilla(memory,roots);
    std::fprintf(out,"guarded-villa=%d unlocked=%d restored=%u value=%u population=%u\n",int(villa.has_value()),
        villa?int(villa->unlocked):0,villa?villa->restored:0u,villa?villa->value:0u,villa?villa->population:0u);
    std::fprintf(out,"guarded-synchronization=%d earned=%u maximum=%u percentage=%.6f\n",
        int(synch.has_value()),synch?synch->earned:0u,synch?synch->maximum:0u,synch?synch->Percentage():0.0);
    auto liveMap = ReadMap(memory, roots);
    std::fprintf(out,"guarded-map=%d\n",int(liveMap.has_value()));
    if(liveMap) {
        auto markers=ReadMapMarkers(memory,roots,*liveMap);
        std::fprintf(out,"guarded-markers=%d count=%u\n",int(markers.has_value()),markers?unsigned(markers->size()):0u);
        std::fprintf(out,"guarded-heading=%d radians=%.6f\n",int(liveMap->heading.has_value()),liveMap->heading?*liveMap->heading:0.f);
        if(markers)for(const auto& marker:*markers)std::fprintf(out,"visible-marker id=%08x type=%u position=%.6f,%.6f\n",marker.id,marker.type,marker.x,marker.y);
        std::fprintf(out,"map-art=%08x title=%u player=%.6f,%.6f mask=%ux%u discovered=%u\n",
            liveMap->artwork,liveMap->title,liveMap->x,liveMap->y,liveMap->maskWidth,liveMap->maskHeight,
            unsigned(std::count(liveMap->discovery.begin(),liveMap->discovery.end(),uint8_t(255))));
        if(auto mask=std::fopen("F:\\Download\\ac2-map-mask.bin","wb")) {
            std::fwrite(liveMap->discovery.data(),1,liveMap->discovery.size(),mask);std::fclose(mask);
        }
    }
    Words(out,memory,"world",roots.world,64);
    PointerArray worldComponents{};
    if(ArrayHeader(memory,roots.world,0x60,256,worldComponents)) {
        for(uint32_t i=0;i<worldComponents.Count();++i){
            uint32_t component{},type{};
            if(!memory.Field(worldComponents.data,i*4,component) || !memory.Field(component,0,type))continue;
            std::fprintf(out,"world-component type=%08x address=%08x\n",type,component);
            if(type!=0x01e7682c)continue;
            Words(out,memory,"map-manager",component,54);
            uint32_t handle{},data{};
            if(memory.Field(component,8,handle) && memory.Reference(handle,data) && data){
                Words(out,memory,"map-data",data,40);
                PointerArray staticMarkers{};
                if(ArrayHeader(memory,data,0x10,4096,staticMarkers)){
                    std::map<uint32_t,unsigned> types;
                    for(uint32_t j=0;j<staticMarkers.Count();++j){
                        uint32_t p{},words[36]{};
                        if(memory.Field(staticMarkers.data,j*4,p) && memory.Read(p,words)){
                            ++types[words[0]];
                            if(j<20)Words(out,memory,"static-map-marker",p,36);
                        }
                    }
                    for(auto kv:types)std::fprintf(out,"map-marker-vtable=%08x count=%u\n",kv.first,kv.second);
                }
                for(uint32_t offset:{0xcu,0x6cu})if(memory.Field(data,offset,handle)){
                    Words(out,memory,"map-art-handle",handle,4);
                    uint32_t art{};if(memory.Reference(handle,art) && art)Words(out,memory,"map-art-object",art,32);
                }
            }
            PointerArray markers{};
            if(ArrayHeader(memory,component,0xc,4096,markers))for(uint32_t j=0;j<std::min(80u,markers.Count());++j){
                uint32_t marker{};if(memory.Field(markers.data,j*4,marker) && marker)Words(out,memory,"map-marker",marker,40);
            }
        }
    }
    uint32_t mapGlobal{};if(memory.Read(0x223f820,mapGlobal))Words(out,memory,"global-map-manager",mapGlobal,84);
    std::fprintf(out, "guarded-player-inventory=%d containers=%lu\n", int(owned.has_value()),
        static_cast<unsigned long>(owned ? owned->containers.size() : 0));
    LocalizationCache text;
    if (owned) for (const auto& container : owned->containers) {
        Words(out, memory, "player-container", container.identity,12);
        if (container.rechargeable) continue;
        PointerArray possessions{};
        if (!ArrayHeader(memory, container.identity, 0x18, 128, possessions)) continue;
        for (uint32_t i=0;i<possessions.Count();++i) {
            uint32_t item{},handle{},definition{};
            if (memory.Field(possessions.data,i*4,item) && item) {
                Words(out,memory,"possession",item,8);
                if(memory.Field(item,4,handle) && memory.Reference(handle,definition) && definition)
                    Words(out,memory,"possession-definition",definition,16);
            }
        }
    }
    auto definitions = ReadReloadDefinitions(memory);
    bool localized = text.Refresh(memory);
    auto equipment = ReadEquipment(memory,roots.player);
    std::fprintf(out,"guarded-equipment=%d visible=%u\n",int(equipment.has_value()),equipment ? unsigned(equipment->size()):0);
    if(equipment) for(const auto& item:*equipment){auto name=text.Lookup(item.title);
        std::fprintf(out,"carried-equipment menu=%u title=%ls equipped=%d\n",item.menu,name?name->c_str():L"?",int(item.equipped));
        if(item.menu>=4 && item.menu<=7)std::fprintf(out,"guarded-weapon-select-eligible=%d; read-only, no action sent\n",
            int(ResolveWeaponSelectionTarget(memory,roots,item.id).has_value()));
        auto stats=ReadWeaponStats(memory,roots,item);
        std::fprintf(out,"guarded-weapon-stats id=%08lx ready=%d damage=%u speed=%u deflection=%u\n",
            static_cast<unsigned long>(item.id),int(stats.has_value()),stats?stats->damage:0,stats?stats->speed:0,stats?stats->deflection:0);}
    std::fprintf(out, "guarded-reload-definitions=%d localized=%d\n", int(definitions.has_value()), int(localized));
    if (definitions && localized) for (const auto& definition : *definitions) {
        auto title = text.Lookup(definition.titleId);
        auto description = text.Lookup(definition.descriptionId);
        std::fprintf(out, "menu=%lu locked=%d title=%ls description-length=%u\n",
            static_cast<unsigned long>(definition.menuId), int(definition.locked),
            title ? title->c_str() : L"<unresolved>", description ? unsigned(description->size()) : 0);
    }
    std::fprintf(out, "language-stable=%d\n", int(text.StillCurrent(memory)));
    auto dna = ReadDna(memory);
    std::fprintf(out, "guarded-DNA=%d visible=%u\n", int(dna.has_value()), dna ? unsigned(dna->size()) : 0);
    auto databaseResult = ReadDatabase(memory, roots.progress);
    std::fprintf(out, "guarded-database=%d visible=%u\n", int(databaseResult.has_value()), databaseResult ? unsigned(databaseResult->size()) : 0);
    if (databaseResult) for (const auto& entry : *databaseResult) {
        auto title = text.Lookup(entry.title); auto description = text.Lookup(entry.description);
        std::fprintf(out, "visible-database category=%u title=%ls text-length=%u\n", entry.category,
            title ? title->c_str() : L"?", description ? unsigned(description->size()) : 0);
        if (entry.imageHandle) Words(out, memory, "database-image-handle", entry.imageHandle, 4);
    }
    uint32_t database{}; PointerArray databaseEntries{};
    if (memory.Read(0x022553b0, database)) {
        Words(out, memory, "database-manager", database, 36);
        PointerArray puzzles{};
        if(ArrayHeader(memory,database,0x28,128,puzzles))for(uint32_t i=0;i<puzzles.Count();++i){
            uint32_t handle{},puzzle{};
            if(memory.Field(puzzles.data,i*4,handle) && memory.Reference(handle,puzzle) && puzzle)
                Words(out,memory,"glyph-puzzle",puzzle,18);
        }
        if (ArrayHeader(memory, database, 0x10, 1024, databaseEntries)) {
            for (uint32_t d = 0; d < databaseEntries.Count(); ++d) {
                uint32_t entry{};
                // 017fc900 reads raw catalog entries, then creates temporary
                // handles only for the menu's filtered list.
                if (!memory.Field(databaseEntries.data, d * 4, entry) || !entry) continue;
                Words(out, memory, "database-entry", entry, 28);
                uint32_t titleId{}, descriptionId{};
                if (memory.Field(entry, 0x3c, titleId) && memory.Field(entry, 0x48, descriptionId)) {
                    auto title = text.Lookup(titleId); auto description = text.Lookup(descriptionId);
                    std::fprintf(out, "database-index=%u title=%ls description-length=%u\n", unsigned(d),
                        title ? title->c_str() : L"?", description ? unsigned(description->size()) : 0);
                }
            }
        }
        for (uint32_t offset : {0x18u, 0x20u}) {
            PointerArray records{};
            if (ArrayHeader(memory, database, offset, 128, records)) for (uint32_t i = 0; i < records.Count(); ++i) {
                uint32_t record{};
                if (memory.Field(records.data, i * 4, record) && record)
                    Words(out, memory, offset == 0x18 ? "database-codex" : "database-document", record, 16);
            }
        }
    }
    uint32_t localization{}; PointerArray languages{};
    if (memory.Read(0x02212914, localization) && ArrayHeader(memory, localization, 0xc, 64, languages)) {
        Words(out, memory, "localization-manager", localization, 12);
        for (uint32_t i = 0; i < languages.Count(); ++i) {
            uint32_t row = languages.data + i * 40;
            Words(out, memory, "language-row", row, 10);
            for (uint32_t offset : {0x10u, 0x14u}) {
                uint32_t handle{}, resource{}, blob{}, size{};
                if (memory.Field(row, offset, handle) && handle && memory.Reference(handle, resource) && resource) {
                    Words(out, memory, "language-resource", resource, 12);
                    if (memory.Field(resource, 0x1c, blob) && memory.Field(resource, 0x20, size) && size)
                        Words(out, memory, "language-compressed", blob, 8);
                }
            }
        }
    }
    uint32_t catalogs{};
    PointerArray catalogArray{};
    if (memory.Read(0x0223f81c, catalogs) && ArrayHeader(memory, catalogs, 0xc, 64, catalogArray)) {
        Words(out, memory, "catalog-manager", catalogs, 8);
        for (uint32_t i = 0; i < catalogArray.Count(); ++i) {
            uint32_t catalog{};
            if (memory.Field(catalogArray.data, i * 4, catalog) && catalog) {
                Words(out, memory, "catalog-candidate", catalog, 12);
                uint32_t type{}; PointerArray groups{};
                if (memory.Field(catalog, 0, type) && type == 0x01e75650 && ArrayHeader(memory, catalog, 8, 32, groups)) {
                    uint32_t config{};if(memory.Read(0x22128b4,config))Words(out,memory,"synchronization-entitlements",config+0x2c0,1);
                    // AdditionalProgressCurrent/Max traverse group -> slice ->
                    // district. Inspect only exact traced classes, with a shared
                    // point budget; no visibility queries or engine calls.
                    uint32_t pointBudget=4096;
                    for(uint32_t offset:{0x24u,0x2cu}){
                        uint32_t handle{},group{},kind{};PointerArray slices{};
                        if(!memory.Member(catalog,offset,handle) || !memory.Reference(handle,group) || !group)continue;
                        std::fprintf(out,"collectible-catalog-field=%x\n",offset);
                        Words(out,memory,"collectible-group",group,12);
                        if(!memory.Read(group,kind) || kind!=0x1e9efd0 || !ArrayHeader(memory,group,0x18,64,slices))continue;
                        for(uint32_t s=0;s<slices.Count();++s){
                            uint32_t slice{};PointerArray districts{};
                            if(!memory.Field(slices.data,s*4,handle) || !memory.Reference(handle,slice) || !slice)continue;
                            Words(out,memory,"collectible-slice",slice,20);
                            if(!memory.Read(slice,kind) || kind!=0x1e9ef50 || !ArrayHeader(memory,slice,0x3c,64,districts))continue;
                            for(uint32_t d=0;d<districts.Count();++d){
                                uint32_t district{};PointerArray points{};
                                if(!memory.Field(districts.data,d*4,handle) || !memory.Reference(handle,district) || !district)continue;
                                Words(out,memory,"collectible-district",district,12);
                                if(memory.Read(district,kind) && kind==0x1e99c78){
                                    uint32_t collection{};
                                    if(memory.Member(district,0x18,handle) && memory.Reference(handle,collection) && collection){
                                        Words(out,memory,"district-collection",collection,64);
                                        PointerArray states{};
                                        if(ArrayHeader(memory,collection,0xe0,255,states))for(uint32_t n=0;n<states.Count() && n<3;++n){
                                            uint32_t point{};
                                            if(memory.Field(states.data,n*4,handle) && memory.Reference(handle,point) && point)
                                                Words(out,memory,"district-collection-point",point,16);
                                        }
                                    }
                                }
                                if(!memory.Read(district,kind) || kind!=0x1e692d8 || !ArrayHeader(memory,district,0x18,4096,points))continue;
                                std::map<std::pair<uint32_t,uint32_t>,unsigned> pointStates;
                                for(uint32_t p=0;p<points.Count() && pointBudget;++p,--pointBudget){
                                    uint32_t point{},state{};
                                    if(!memory.Field(points.data,p*4,handle) || !memory.Reference(handle,point) || !point)continue;
                                    if(!memory.Read(point,kind) || !memory.Member(point,0x1c,state))continue;
                                    if(++pointStates[{kind,state}]==1)Words(out,memory,"collectible-point",point,12);
                                }
                                for(const auto& row:pointStates)std::fprintf(out,"collectible-point-class=%08x state=%u count=%u\n",
                                    row.first.first,row.first.second,row.second);
                            }
                        }
                    }
                    for(uint32_t k=0;k<5;++k){
                        uint32_t h{},group{};PointerArray entries{};
                        if(!memory.Member(catalog,0x10+k*4,h) || !memory.Reference(h,group) || !group)continue;
                        Words(out,memory,"synchronization-side-group",group,8);
                        if(!ArrayHeader(memory,group,0x18,4096,entries))continue;
                        for(uint32_t j=0;j<entries.Count();++j){
                            uint32_t ref{},entry{},stateHandle{},state{};
                            if(!memory.Field(entries.data,j*4,ref) || !memory.Reference(ref,entry) || !entry)continue;
                            Words(out,memory,"synchronization-side-entry",entry,17);
                            if(memory.Member(entry,0x3c,stateHandle) && memory.Reference(stateHandle,state) && state)
                                Words(out,memory,"synchronization-side-state",state,10);
                        }
                    }
                    for (uint32_t s = 0; s < groups.Count(); ++s) {
                        uint32_t handle{}, sequence{}; PointerArray memories{};
                        if (!memory.Field(groups.data, s * 4, handle) || !memory.Reference(handle, sequence) || !sequence) continue;
                        Words(out, memory, "dna-sequence", sequence, 12);
                        uint32_t titleId{}, stateHandle{}, state{};
                        if (memory.Field(sequence, 0xc, titleId)) {
                            auto title = text.Lookup(titleId);
                            std::fprintf(out, "sequence-index=%lu title=%ls\n", static_cast<unsigned long>(s), title ? title->c_str() : L"?");
                        }
                        if (memory.Field(sequence, 0x18, stateHandle) && memory.Reference(stateHandle, state) && state)
                            Words(out, memory, "dna-sequence-state", state, 12);
                        if (!ArrayHeader(memory, sequence, 0x24, 64, memories)) continue;
                        for (uint32_t m = 0; m < memories.Count(); ++m) {
                            uint32_t reference{}, mission{}, vtable{};
                            if (!memory.Field(memories.data, m * 4, reference) || !memory.Reference(reference, mission) || !mission) continue;
                            Words(out, memory, "dna-memory", mission, 26);
                            if (memory.Field(mission, 0, vtable)) Words(out, memory, "dna-memory-vtable", vtable, 20);
                            uint32_t missionHandle{}, missionState{};
                            if (memory.Field(mission, 0x3c, missionHandle) && memory.Reference(missionHandle, missionState) && missionState)
                                Words(out, memory, "dna-memory-state", missionState, 40);
                            if (memory.Field(mission, 0xc, titleId)) {
                                auto title = text.Lookup(titleId);
                                std::fprintf(out, "memory-index=%lu title=%ls\n", static_cast<unsigned long>(m), title ? title->c_str() : L"?");
                            }
                        }
                    }
                }
                if (!memory.Field(catalog, 0, type) || type != 0x01e610c8 ||
                    !ArrayHeader(memory, catalog, 8, 32, groups)) continue;
                for (uint32_t g = 0; g < groups.Count(); ++g) {
                    uint32_t reference{}, group{}, id{};
                    if (!memory.Field(groups.data, g * 4, reference) || !memory.Reference(reference, group) || !group ||
                        !memory.Field(group, 0x10, id)) continue;
                    Words(out, memory, "inventory-menu-group", group, 8);
                    if (id<1 || id>5) continue;
                    PointerArray items{};
                    if (!ArrayHeader(memory, group, 8, 64, items)) continue;
                    for (uint32_t k = 0; k < items.Count(); ++k) {
                        uint32_t handle{}, item{}, dataHandle{}, definition{};
                        if (memory.Field(items.data, k * 4, handle) && memory.Reference(handle, item) && item) {
                            Words(out, memory, "reload-menu-item", item, 12);
                            uint32_t nameId{}, menuId{};
                            if (memory.Field(item,0x18,nameId) && memory.Field(item,0x14,menuId)) {
                                auto name=text.Lookup(nameId);
                                std::fprintf(out,"catalog-item group=%u menu=%u title=%ls\n",id,menuId,name ? name->c_str() : L"?");
                            }
                            for (uint32_t offset : {0xcu, 0x10u}) {
                                uint32_t imageHandle{};
                                if (memory.Field(item, offset, imageHandle) && imageHandle)
                                    Words(out, memory, "reload-image-handle", imageHandle, 4);
                            }
                            if (memory.Field(item, 8, dataHandle) && memory.Reference(dataHandle, definition) && definition)
                                Words(out, memory, "reload-definition", definition, 16);
                        }
                    }
                }
            }
        }
    }
    Words(out, memory, "player", roots.player, 64);
    CharacterGeometryStage geometryStage{};
    if(auto geometry=ReadCharacterGeometry(memory,roots,&geometryStage)){
        std::fprintf(out,"character-geometry bindings=%u pending=%lu unsupported=%lu\n",unsigned(geometry->bindings.size()),
            static_cast<unsigned long>(geometry->pending),static_cast<unsigned long>(geometry->unsupported));
        for(const auto& b:geometry->bindings){
            std::fprintf(out,"geometry-binding mesh=%08lx instance=%08lx renderer=%08lx kind=%d registration=%lu geometry=%08lx\n",
                static_cast<unsigned long>(b.mesh),static_cast<unsigned long>(b.instance),static_cast<unsigned long>(b.renderer),int(b.kind),
                static_cast<unsigned long>(b.registration),static_cast<unsigned long>(b.geometry));
            Words(out,memory,"character-geometry-object",b.geometry,b.kind==CharacterRendererKind::Skinned?12:10);
            if(b.kind==CharacterRendererKind::Skinned){
                std::fprintf(out,"geometry-stream stride=%lu vertex-wrapper=%08lx index-wrapper=%08lx\n",
                    static_cast<unsigned long>(b.vertexStride),static_cast<unsigned long>(b.vertexBuffer),
                    static_cast<unsigned long>(b.indexBuffer));
                std::fprintf(out,"geometry-interfaces vertex=%08lx index=%08lx\n",
                    static_cast<unsigned long>(b.vertexInterface),static_cast<unsigned long>(b.indexInterface));
                for(unsigned set=0;set<2;++set){
                    const auto& ranges=set?b.secondaryRanges:b.primaryRanges;
                    for(const auto& r:ranges)std::fprintf(out,"geometry-draw set=%u primitive=%lu min-vertex=%lu vertices=%lu start-index=%lu primitives=%lu\n",
                        set,static_cast<unsigned long>(r.primitive),static_cast<unsigned long>(r.minVertex),
                        static_cast<unsigned long>(r.vertexCount),static_cast<unsigned long>(r.startIndex),
                        static_cast<unsigned long>(r.primitiveCount));
                }
            }
        }
    }else std::fprintf(out,"character-geometry unavailable/incoherent stage=%d\n",int(geometryStage));
    if(auto character=ReadCharacterSource(memory,roots)){
        std::fprintf(out,"character-source meshes=%u unsupported=%lu skeleton=%08lx asset=%08lx\n",
            unsigned(character->meshes.size()),static_cast<unsigned long>(character->unsupportedInstances),
            static_cast<unsigned long>(character->skeletonComponent),static_cast<unsigned long>(character->skeletonAsset));
        for(const auto& mesh:character->meshes){
            std::fprintf(out,"character-mesh visual=%08lx instance=%08lx registration=%lu bones=%u\n",
                static_cast<unsigned long>(mesh.component),static_cast<unsigned long>(mesh.instance),
                static_cast<unsigned long>(mesh.registration),unsigned(mesh.bones.size()));
        }
    }else std::fprintf(out,"character-source unavailable/incoherent; no render call attempted\n");
    {
        ReadTransaction transaction(memory);
        auto character=ReadCharacterSourceFields(transaction,roots);
        std::vector<std::vector<CharacterPalette>> sets;bool coherent=bool(character);
        if(character)for(const auto& mesh:character->meshes){
            uint32_t type{};
            if(!mesh.renderer || !transaction.Read(mesh.renderer,type)){coherent=false;break;}
            if(type!=0x1dcfbb4)continue;
            sets.emplace_back();
            if(!ReadCharacterPaletteFields(transaction,mesh,sets.back())){coherent=false;break;}
        }
        coherent=coherent && transaction.Validate();
        std::fprintf(out,"character-palettes coherent=%d skinned-meshes=%u\n",int(coherent),coherent?unsigned(sets.size()):0);
        if(coherent)for(const auto& set:sets)for(const auto& palette:set){
            std::fprintf(out,"character-palette bones=%u skeleton-indices=",unsigned(palette.size()));
            for(const auto& bone:palette)std::fprintf(out,"%u,",unsigned(bone.skeletonIndex));
            std::fputc('\n',out);
        }
    }
    uint32_t transformController{};
    if(memory.Member(roots.player,0x50,transformController) && transformController){
        Words(out,memory,"player-transform-controller",transformController,32);
        uint32_t sourceKind{},provider{};
        if(memory.Member(transformController,0x50,sourceKind) && sourceKind==1 &&
            memory.Member(transformController,0x54,provider) && provider)
            Words(out,memory,"player-transform-provider",provider,4);
    }
    PointerArray playerComponents{};
    if(ArrayHeader(memory,roots.player,0x74,256,playerComponents)){
        for(uint32_t i=0;i<playerComponents.Count();++i){uint32_t component{},type{};
            if(memory.Field(playerComponents.data,i*4,component) && component){
                Words(out,memory,"player-render-research-component",component,32);
                if(memory.Read(component,type) && type==0x1db4508){
                    uint32_t owner{},instance{},instanceType{};
                    if(memory.Field(component,8,owner) && owner==roots.player &&
                       memory.Field(component,0x10,instance) && instance){
                        Words(out,memory,"player-visual-instance",instance,32);
                        if(memory.Read(instance,instanceType) && instanceType==0x1db4e88){
                            uint32_t mesh{},registration{},table{},count{},registered{},renderers{},renderer{};
                            if(memory.Field(instance,0x10,mesh) && mesh)
                                Words(out,memory,"player-mesh-native-asset",mesh,64);
                            PointerArray bones{};
                            if(ArrayHeader(memory,instance,0x30,256,bones) && bones.Count())
                                Words(out,memory,"player-mesh-bone-map",bones.data,bones.Count());
                            // b280d0: source registry and lazy renderer registry.
                            // Read only an already-created matching registration.
                            if(memory.Field(instance,8,registration) && registration &&
                               memory.Read(0x221da5c,table) && memory.Read(0x221da60,count) && count<=262144 &&
                               registration<=count && memory.Field(table,(registration-1)*4,registered) && registered==instance &&
                               memory.Read(0x221da68,renderers) && memory.Read(0x221da6c,count) && count<=262144 &&
                               registration<=count && memory.Field(renderers,(registration-1)*4,renderer) && renderer)
                                Words(out,memory,"player-mesh-native-renderer",renderer,32);
                        }
                    }
                }
                if(memory.Read(component,type) && type==0x1cb3568){
                    // SkeletonComponent constructor56d640; binding affee0 uses
                    // +1f0 skeleton and bone map. Evidence only, no rendering calls.
                    Words(out,memory,"player-skeleton-native-runtime",component+0x1e0,32);
                    uint32_t skeleton{};
                    if(memory.Field(component,0x1f0,skeleton) && skeleton)
                        Words(out,memory,"player-skeleton-native-asset",skeleton,32);
                }
                if(type==0x1db9eb8){
                    // MaterialOverrider constructorb8b850, used by afec10.
                    PointerArray overrides{};
                    if(ArrayHeader(memory,component,0xc,64,overrides) && overrides.Count())
                        Words(out,memory,"player-material-overrides",overrides.data,std::min(64u,overrides.Count()*2));
                }
            }
        }
    }
    Words(out, memory, "progress", roots.progress, 112);
    uint32_t selector{}, table{}; uint16_t count{}; uint8_t index{}, epoch{}, cachedEpoch{};
    if (!memory.Read(roots.player + 0xd6, index) || index >= 0xfe ||
        !memory.Read(roots.player + 0xf4, epoch) || !memory.Read(roots.player + 0xd5, cachedEpoch) ||
        epoch != cachedEpoch || !memory.Read(roots.player + 0x7a, count) ||
        !memory.Field(roots.player, 0x74, table) || index >= (count & 0x3fff) ||
        !memory.Field(table, index * 4, selector)) {
        std::fprintf(out, "Native component cache unresolved; no refresh/mutation attempted\n"); return 7;
    }
    Words(out, memory, "script-component", selector, 32);
    uint32_t entity{}, context{}, registry{}, events{}, records{}, length{};
    if (!memory.Field(selector, 0x10, entity) || !memory.Field(entity, 0x20, context) ||
        !memory.Field(context, 0x20, registry) || !memory.Field(registry, 0x4c, events) ||
        !memory.Field(events, 0x14, records) || !memory.Field(events, 0x38, length) || length > 4096) {
        std::fprintf(out, "No bounded native service registry\n"); return 8;
    }
    Words(out, memory, "script-entity", entity, 32);
    Words(out, memory, "script-context", context, 32);
    Words(out, memory, "registry", registry, 32);
    // Identify the actual subscriber before treating generic event dispatchers
    // as getters. Native service IDs are resolved afresh, never hard-coded IDs.
    std::array<uint32_t, 24> ids{};
    if (!memory.Read(0x223f664, ids)) return 9;
    for (uint32_t i = 0; i < length; ++i) {
        uint32_t row[6]{};
        if (!memory.Copy(records + i * 24, row, sizeof(row))) return 10;
        if (row[1] & 0x80000000) continue;
        uint32_t type = (row[1] >> 3) & 0xffff;
        if((row[1]&7)==5 && (row[5]==0x15f21b0 || row[5]==0x15f21c0 || row[5]==0x15f2880 || row[5]==0x15f27c0 ||
            row[5]==0x15f2840 || row[5]==0x15f2820 || row[5]==0x15f2800 || row[5]==0x15f2780 || row[5]==0x15f28a0)){
            std::fprintf(out,"weapon-event id=%u function=%08x\n",type,row[5]);
            Words(out,memory,"weapon-subscriber",row[2],8);
            uint32_t weaponOwner{};
            if(memory.Member(row[2],0xc,weaponOwner))Words(out,memory,"weapon-owner",weaponOwner,32);
        }
        if((row[1]&7)==5 && row[5]>=0x15fd000 && row[5]<0x1605100){
            std::fprintf(out,"health-event id=%u function=%08x\n",type,row[5]);
            Words(out,memory,"health-subscriber",row[2],8);
            uint32_t owner{},ownerType{};
            if(memory.Member(row[2],0xc,owner) && memory.Read(owner,ownerType) && ownerType==0x1ea1658)
                Words(out,memory,"health-owner",owner,50);
        }
        for (unsigned j = 0; j < ids.size(); ++j) if (type == ids[j]) {
            std::fprintf(out, "inventory-event global=%08lx id=%lu index=%lu\n",
                static_cast<unsigned long>(0x223f664 + j * 4), static_cast<unsigned long>(type),
                static_cast<unsigned long>(i));
            Words(out, memory, "subscriber", records + i * 24, 6);
            // Private object snapshot: bounded, never interpreted as an item or
            // exposed on the companion until the subscriber layout is verified.
            // 010b5be0: kind 5 puts row+8 into ECX and calls row+14.
            // row+0 is a scheduling/priority value, not an object pointer.
            if ((row[1] & 7) == 5) {
                // Other subscribers share this object; only the count service
                // needs an object snapshot, keeping the report bounded/small.
                if (0x223f664 + j * 4 != 0x223f6a0) continue;
                Words(out, memory, "subscriber-object", row[2], 8);
                uint32_t owner{};
                if (memory.Field(row[2], 0xc, owner)) {
                    Words(out, memory, "inventory-owner", owner, 12);
                    uint32_t context{}, data{}, inventory{}; uint16_t size{};
                    // Exact 010baa60 chain; the reader still gates the embedded
                    // inventory's vtable and does not invoke the type checker.
                    if (memory.Field(owner, 0x20, context) && memory.Read(context + 0x5e, size) &&
                        (size & 0x3fff) && memory.Field(context, 0x58, data) && memory.Field(data, 0xc, inventory)) {
                        Words(out, memory, "inventory-data-item", inventory, 16);
                        auto contents = ReadInventory(memory, inventory + 0xc);
                        std::fprintf(out, "inventory-decoded=%d\n", int(contents.has_value()));
                        if (contents) for (const auto& item : contents->containers)
                            std::fprintf(out, "category=%08lx level=%lu count=%lu capacity=%lu rechargeable=%d\n",
                                static_cast<unsigned long>(item.category), static_cast<unsigned long>(item.level),
                                static_cast<unsigned long>(item.quantity), static_cast<unsigned long>(item.capacity), int(item.rechargeable));
                    }
                }
            }
        }
    }
    Roots after{};
    bool coherent = ReadRoots(memory, after) && roots == after;
    uint32_t afterRecords{}, afterLength{};
    coherent = coherent && memory.Field(events, 0x14, afterRecords) && afterRecords == records &&
        memory.Field(events, 0x38, afterLength) && afterLength == length;
    std::fprintf(out, "stable-roots-and-registry=%d; external snapshot is research evidence only\n", int(coherent));
    return coherent ? 0 : 11;
}

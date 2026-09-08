#pragma once
#include "NativeCapes.h"

namespace ac2ds::native {
struct CapeTarget {
    uint32_t entity{}, handle{}, menu{}, currentMenu{},subscriber{};
    bool operator==(const CapeTarget& other)const {
        return entity==other.entity && handle==other.handle && menu==other.menu && currentMenu==other.currentMenu && subscriber==other.subscriber;
    }
};
// The menu's CanEquip path (012da200) excludes Carnival and UI+fe restrictions,
// resolves the player's script entity (00948dc0), then asks event8231. Its Equip
// path (012d9510) asks the same event before sending event8233. Never call the
// low-level item removal/insertion jobs directly: the native dispatcher owns
// their reference counting, scheduling and visible-character updates.
inline std::optional<CapeTarget> ResolveCapeTarget(const Memory& source,const Roots& roots,uint32_t id){
    ReadTransaction memory(source);Roots before{};
    if(!id || !ReadRoots(memory,before) || !(before==roots) || !roots.player || !roots.world || !roots.progress)return {};
    uint32_t ui{},type{};uint8_t restricted{};
    if(!memory.Read(0x223f820,ui) || !memory.Read(ui,type) || type!=0x1e60898 ||
        !memory.Member(ui,0xfe,restricted) || restricted)return {};
    auto owner=ResolvePlayerInventory(memory,roots.player);if(!owner)return {};
    uint32_t registry{},events{},records{},count{},permissionId{},mutationId{},subscriber{};
    if(!memory.Member(owner->context,0x20,registry) || !memory.Member(registry,0x4c,events) ||
        !memory.Member(events,0x14,records) || !memory.Member(events,0x38,count) || !count || count>4096 ||
        records<0x10000 || records>UINT32_MAX-count*24 ||
        !memory.Read(0x223f664,permissionId) || !memory.Read(0x223f66c,mutationId) ||
        !permissionId || !mutationId || permissionId>65535 || mutationId>65535 || permissionId==mutationId)return {};
    bool permissionFound=false,mutationFound=false;
    for(uint32_t i=0;i<count;++i){
        std::array<uint32_t,6> row{};if(!memory.Read(records+i*24,row))return {};
        if(row[1]&0x80000000u)continue;
        auto event=(row[1]>>3)&0xffffu;if(event!=permissionId && event!=mutationId)continue;
        bool& found=event==permissionId?permissionFound:mutationFound;
        if(found || (row[1]&7)!=5 || row[2]<0x10000 || row[5]!=(event==permissionId?0x15fbc00u:0x15fcc30u) ||
            (subscriber && subscriber!=row[2]))return {};
        subscriber=row[2];found=true;
    }
    uint32_t inventoryOwner{},inventoryContext{};
    if(!permissionFound || !mutationFound || !memory.Read(subscriber,type) || type!=0x1ea141c ||
        !memory.Member(subscriber,0xc,inventoryOwner) || !memory.Read(inventoryOwner,type) || type!=0x1ea1448 ||
        !memory.Member(inventoryOwner,0x20,inventoryContext) || inventoryContext!=owner->context)return {};
    auto capes=ReadCapes(memory,roots.player);if(!capes)return {};
    const EquipmentRecord* selected=nullptr;uint32_t worn{};
    for(const auto& cape:*capes){
        if(cape.equipped){if(worn)return {};worn=cape.menu;}
        if(cape.id==id){if(selected)return {};selected=&cape;}
    }
    // No cape-bearing player yet (Desmond/young Ezio), forced mission cape,
    // unknown ownership, Altair model override and selecting the worn cape
    // never reach an engine mutation.
    if(!worn || worn==26 || !selected || selected->equipped || !selected->definitionHandle ||
        (selected->menu!=22 && selected->menu!=23 && selected->menu!=25 && selected->menu!=33))return {};
    if(!memory.Validate())return {};
    return CapeTarget{owner->entity,selected->definitionHandle,selected->menu,worn,subscriber};
}
enum class CapeActionResult { Unavailable, Denied, StateChanged, Dispatched, NativeRejected };
struct CapeEngine {
    virtual ~CapeEngine()=default;
    virtual bool CanEquip(const CapeTarget& target)=0;
    virtual bool Equip(const CapeTarget& target)=0;
};
inline CapeActionResult EquipCape(const Memory& memory,const Roots& roots,uint32_t id,CapeEngine& engine){
    auto target=ResolveCapeTarget(memory,roots,id);if(!target)return CapeActionResult::Unavailable;
    if(!engine.CanEquip(*target))return CapeActionResult::Denied;
    // A native event can run callbacks. Recheck the actual roots, ownership,
    // blocker and handle after the permission query before dispatching.
    auto current=ResolveCapeTarget(memory,roots,id);
    if(!current || !(*current==*target))return CapeActionResult::StateChanged;
    return engine.Equip(*current)?CapeActionResult::Dispatched:CapeActionResult::NativeRejected;
}
}

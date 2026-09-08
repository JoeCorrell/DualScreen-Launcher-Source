#pragma once
#include "NativeEquipment.h"
#include "NativeReadTransaction.h"
namespace ac2ds::native {
// Native 012da100 queries category 13 in order Auditore, Carnival, Medici,
// Venetian, Standard; 010e09b0 selects definition+10 subtype and requires
// exactly one resident item. Owning an unlocked cape does not mean wearing it.
inline std::optional<uint32_t> CurrentCape(const Memory& memory,const InventoryContents& inventory){
    std::array<uint32_t,6> counts{};
    for(const auto& container:inventory.containers){
        if(container.category!=13)continue;
        if(container.rechargeable)return {};
        PointerArray items{};if(!ArrayHeader(memory,container.identity,0x18,2048,items))return {};
        if(items.Count()!=container.quantity)return {};
        for(uint32_t i=0;i<items.Count();++i){
            uint32_t item{},handle{},definition{},type{},category{},subtype{};
            if(!memory.Field(items.data,i*4,item))return {};
            if(!item)continue;
            if(!memory.Member(item,4,handle) || !memory.Reference(handle,definition))return {};
            if(!definition)continue;
            if(!memory.Read(definition,type) || type!=0x1e699bc || !memory.Member(definition,0xc,category) ||
                category!=13 || !memory.Member(definition,0x10,subtype))return {};
            if(subtype<counts.size())++counts[subtype];
        }
    }
    constexpr std::pair<uint32_t,uint32_t> priority[]{{5,33},{4,26},{1,23},{3,25},{0,22}};
    for(auto pair:priority)if(counts[pair.first]==1)return pair.second;
    return 0;
}
inline std::optional<std::vector<EquipmentRecord>> ReadCapes(const Memory& source,uint32_t player){
    ReadTransaction memory(source);
    auto inventory=ReadPlayerInventory(memory,player);if(!inventory)return {};
    auto current=CurrentCape(memory,*inventory);if(!current)return {};
    uint32_t manager{},catalog{},group{};PointerArray catalogs{},groups{},entries{};
    if(!memory.Read(0x223f81c,manager) || !ArrayHeader(memory,manager,0xc,64,catalogs))return {};
    for(uint32_t i=0;i<catalogs.Count();++i){
        uint32_t candidate{},type{};
        if(!memory.Field(catalogs.data,i*4,candidate))return {};
        if(!candidate)continue;
        if(!memory.Read(candidate,type))return {};
        if(type==0x1e610c8){if(catalog)return {};catalog=candidate;}
    }
    if(!catalog || !ArrayHeader(memory,catalog,8,32,groups))return {};
    for(uint32_t i=0;i<groups.Count();++i){
        uint32_t handle{},candidate{},id{};
        if(!memory.Field(groups.data,i*4,handle) || !memory.Reference(handle,candidate))return {};
        if(!candidate)continue;
        if(!memory.Member(candidate,0x10,id))return {};
        if(id==5){group=candidate;break;}
    }
    if(!group || !ArrayHeader(memory,group,8,32,entries))return {};
    std::vector<EquipmentRecord> result;std::unordered_set<uint32_t> menus;
    for(uint32_t i=0;i<entries.Count();++i){
        uint32_t handle{},entry{};std::array<uint32_t,12> row{};
        if(!memory.Field(entries.data,i*4,handle) || !memory.Reference(handle,entry))return {};
        if(!entry)continue;
        if(!memory.Read(entry,row) || row[0]!=0x1e61020)return {};
        const auto menu=row[5];
        // Original user-selectable cape menu. The mission-only Carnival cape
        // is displayed when actually worn. Altair's model override is separate.
        if(menu!=22 && menu!=23 && menu!=25 && menu!=33 && !(menu==26 && *current==26))continue;
        if(!menus.insert(menu).second)return {};
        uint32_t definition{},type{},category{};uint8_t locked{};
        if(!memory.Reference(row[2],definition))return {};
        if(!definition)continue;
        if(!memory.Read(definition,type) || type!=0x1e699bc || !memory.Member(definition,0xc,category) ||
            category!=13 || !memory.Member(definition,0x30,locked))return {};
        if(locked)continue; // Original InventoryIsLocked (012d96c0).
        EquipmentRecord record;record.menu=menu;record.title=row[6];record.description=row[9];record.equipped=menu==*current;
        record.definitionHandle=row[2];
        if(!ReadArtworkId(memory,row[2],record.id) || !record.id ||
            !ReadArtworkId(memory,row[3],record.artworkId) || !ReadArtworkId(memory,row[4],record.thumbnailId))return {};
        result.push_back(record);
    }
    if(!memory.Validate())return {};
    return result;
}
}

#pragma once
#include "NativeDatabase.h"
#include <unordered_set>
namespace ac2ds::native {
struct EquipmentRecord {
    uint32_t id{}, menu{}, title{}, description{}, artworkId{}, thumbnailId{};
    uint32_t definitionHandle{}; // Borrowed native identity, never copied into UI snapshots.
    bool equipped{};
};
// 010df420 is the native ownership predicate reached by the live inventory
// subscriber 015f77a0. It compares resolved definition identities within the
// corresponding player's category; archive/catalog presence is not ownership.
inline std::optional<std::unordered_set<uint32_t>> CarriedDefinitions(const Memory& memory, const InventoryContents& inventory) {
    std::unordered_set<uint32_t> result;
    for (const auto& container : inventory.containers) {
        if (container.rechargeable) continue;
        PointerArray items{};
        if (!ArrayHeader(memory, container.identity,0x18,2048,items) || items.Count()!=container.quantity) return {};
        struct Copy { uint32_t slot{},item{},handle{},definition{},category{}; };
        std::vector<Copy> copies;
        for (uint32_t i=0;i<items.Count();++i) {
            Copy copy;copy.slot=items.data+i*4;
            if (!memory.Read(copy.slot,copy.item) || !memory.Field(copy.item,4,copy.handle) ||
                !memory.Reference(copy.handle,copy.definition)) return {};
            if (copy.definition) {
                uint32_t type{};
                if (!memory.Field(copy.definition,0,type) || type!=0x01e699bc ||
                    !memory.Field(copy.definition,0xc,copy.category) || copy.category!=container.category) return {};
                result.insert(copy.definition);
            }
            copies.push_back(copy);
        }
        for (const auto& copy : copies) {
            uint32_t item{},handle{},definition{},category{};
            if (!memory.Read(copy.slot,item) || item!=copy.item || !memory.Field(item,4,handle) || handle!=copy.handle ||
                !memory.Reference(handle,definition) || definition!=copy.definition ||
                (definition && (!memory.Field(definition,0xc,category) || category!=copy.category))) return {};
        }
        uint32_t category{},type{};
        if (!SameArray(memory,container.identity,0x18,items) || !memory.Field(container.identity,8,category) ||
            category!=container.category || !memory.Field(container.identity,0,type) || type!=0x01e58098) return {};
    }
    return result;
}

inline std::optional<std::vector<EquipmentRecord>> ReadEquipment(const Memory& memory,uint32_t player) {
    auto owner=ResolvePlayerInventory(memory,player);
    auto inventory=ReadPlayerInventory(memory,player);
    if (!owner || !inventory) return {};
    auto carried=CarriedDefinitions(memory,*inventory);if(!carried)return {};
    uint32_t manager{},catalog{},catalogSlot{};PointerArray catalogs{},groups{};
    if (!memory.Read(0x223f81c,manager) || !ArrayHeader(memory,manager,0xc,64,catalogs))return {};
    for(uint32_t i=0;i<catalogs.Count();++i){
        uint32_t entry{},type{};auto slot=catalogs.data+i*4;
        if(!memory.Read(slot,entry) || (entry && !memory.Field(entry,0,type)))return {};
        if(type==0x01e610c8){if(catalog)return {};catalog=entry;catalogSlot=slot;}
    }
    if(!catalog || !ArrayHeader(memory,catalog,8,32,groups))return {};
    std::vector<EquipmentRecord> result;
    std::unordered_set<uint32_t> equippedMenus, seenGroups;
    for(uint32_t g=0;g<groups.Count();++g){
        uint32_t handle{},group{},groupId{};auto slot=groups.data+g*4;
        if(!memory.Read(slot,handle) || !memory.Reference(handle,group))return {};
        if(!group)continue;
        if(!memory.Field(group,0x10,groupId))return {};
        if(groupId!=1 && groupId!=2)continue;
        if(!seenGroups.insert(groupId).second)return {};
        PointerArray entries{};std::vector<DatabaseCopy<12>> rows;
        if(!ArrayHeader(memory,group,8,128,entries))return {};
        struct Definition {uint32_t handle{},object{};uint8_t locked{};};
        std::vector<Definition> definitions;
        for(uint32_t i=0;i<entries.Count();++i){
            uint32_t reference{},entry{};auto entrySlot=entries.data+i*4;
            if(!memory.Read(entrySlot,reference) || !memory.Reference(reference,entry))return {};
            if(!entry)continue;
            DatabaseCopy<12> row;row.slot=entrySlot;row.address=reference;
            if(!memory.Read(entry,row.words) || row.words[0]!=0x01e61020)return {};
            rows.push_back(row);
            Definition definition;definition.handle=row.words[2];uint32_t type{};
            if(!memory.Reference(definition.handle,definition.object))return {};
            if(!definition.object)continue;
            if(!memory.Field(definition.object,0,type) || type!=0x01e699bc ||
                !memory.Member(definition.object,0x30,definition.locked))return {};
            definitions.push_back(definition);
            auto menu=row.words[5];
            if(definition.locked || !carried->count(definition.object) ||
                !(menu<=8 || menu==27 || menu==28))continue;
            EquipmentRecord record;record.menu=menu;record.title=row.words[6];record.description=row.words[9];
            record.definitionHandle=definition.handle;
            if(!ReadArtworkId(memory,definition.handle,record.id) || !record.id ||
                !ReadArtworkId(memory,row.words[3],record.artworkId) || !ReadArtworkId(memory,row.words[4],record.thumbnailId))return {};
            // 012d9190 returns the first unlocked carried match for 0..5.
            record.equipped=menu<=5 && equippedMenus.insert(menu).second;
            result.push_back(record);
        }
        for(const auto& row:rows){
            uint32_t reference{},entry{};std::array<uint32_t,12> after{};
            if(!memory.Read(row.slot,reference) || reference!=row.address || !memory.Reference(reference,entry) ||
                !memory.Read(entry,after) || after!=row.words)return {};
        }
        for(const auto& definition:definitions){uint32_t object{};uint8_t locked{};
            if(!memory.Reference(definition.handle,object) || object!=definition.object ||
                !memory.Member(object,0x30,locked) || locked!=definition.locked)return {};
        }
        uint32_t afterHandle{},afterGroup{},afterId{};
        if(!SameArray(memory,group,8,entries) || !memory.Read(slot,afterHandle) || afterHandle!=handle ||
            !memory.Reference(handle,afterGroup) || afterGroup!=group || !memory.Field(group,0x10,afterId) || afterId!=groupId)return {};
    }
    auto afterOwner=ResolvePlayerInventory(memory,player);auto afterInventory=ReadPlayerInventory(memory,player);
    if(!afterOwner || !(*owner==*afterOwner) || !afterInventory || inventory->identity!=afterInventory->identity)return {};
    auto afterCarried=CarriedDefinitions(memory,*afterInventory);
    uint32_t check{};
    if(!afterCarried || *carried!=*afterCarried || !SameArray(memory,manager,0xc,catalogs) ||
        !SameArray(memory,catalog,8,groups) || !memory.Read(catalogSlot,check) || check!=catalog ||
        !memory.Read(0x223f81c,check) || check!=manager || seenGroups.size()!=2)return {};
    return result;
}
}

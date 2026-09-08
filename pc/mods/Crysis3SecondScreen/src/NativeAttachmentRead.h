#pragma once
#include "NativeWeaponRead.h"

namespace c3::research {
// Additional read-only closure. Requires both weapon profiles first. No action ABI.
inline constexpr NativeWeaponFingerprint NativeAttachmentProfile[]{
    {0x382259e0,80,"3faca37436c72ac2129333365d34d647a3e28a9c63b766ae71e29c64ff2eeb05"},
    {0x38225a30,112,"3688cf4ce13aff94b80f2b478806132aa28e9616b763b555349e4527bb2f3f18"},
    {0x38226380,512,"5964839d77301c56af53afcf941c727e160c5941520f17682dfe9e3ceed311f9"},
    {0x399e3d08,0x244,"f6d9c52fcbcf116afb374ccd12bdeaca97dddb5d374a9b65aac4a3ac3152e412"},
};
struct NativeAttachmentValue {
    std::array<char,64> name{};
    bool mounted{}, owned{}, defaultEntry{}, nativeEnabled{};
};
struct NativeAttachmentValues {
    uint32_t weaponId{};
    size_t count{};
    std::array<NativeAttachmentValue,12> items{};
};

// The four mounted entries are a conservative observation budget, NOT a claim
// about native container capacity. Reject larger lists rather than read past
// the verified region. Compatibility records follow 382259e0, stride 0x74.
// The caller must enclose this in its actor/phase/epoch callback transaction.
template<class Reader> bool ReadOwnedAttachments(Reader& read,bool verified,
    uint32_t actor,uint32_t owner,uint32_t system,uint32_t id,NativeAttachmentValues& out) {
    out={};
    OwnedWeaponName before{},after{};
    if(!verified || !ReadOwnedWeaponName(read,true,actor,owner,system,id,before)) return false;
    uint32_t item{};
    if(!ReadItemPointer(read,system,id,item) || !item) return false;
    std::array<uint8_t,0x1b8> bytes{},bytesAfter{};
    if(!WeaponStableCopy(read,item,bytes) || WeaponWord(bytes.data())!=0x399e3d08 ||
       WeaponWord(bytes.data()+8)!=id || WeaponWord(bytes.data()+0x100)!=owner ||
       WeaponWord(bytes.data()+0x104)!=actor) return false;
    const auto mounted=WeaponWord(bytes.data()+0x70);
    if(mounted>4) return false;
    std::array<uint8_t,0x44> actorBytes{};
    InventoryObservation inv{}; InventoryEntries owned{},ownedAfter{};
    if(!WeaponStableCopy(read,actor,actorBytes) ||
       !ReadInventory(read,WeaponWord(actorBytes.data()+0x40),owner,inv) ||
       !ReadInventoryEntries(read,inv,true,owned)) return false;
    const auto shared=WeaponWord(bytes.data()+0x14);
    std::array<uint8_t,16> header{},headerAfter{};
    if(!WeaponStableCopy(read,shared,header)) return false;
    const auto begin=WeaponWord(header.data()+8),end=WeaponWord(header.data()+12);
    if(end<begin || (end-begin)%0x74 || (end-begin)/0x74>12) return false;
    const size_t count=(end-begin)/0x74;
    std::array<uint8_t,12*0x74> records{},recordsAfter{};
    if(count && (!WeaponCopy(read,begin,records.data(),count*0x74) ||
        !WeaponCopy(read,begin,recordsAfter.data(),count*0x74) || records!=recordsAfter)) return false;
    NativeAttachmentValues result{}; result.weaponId=id; result.count=count;
    std::array<uint32_t,12> classes{};
    for(size_t i=0;i<count;++i) {
        const auto* record=records.data()+i*0x74;
        classes[i]=WeaponWord(record);
        for(size_t j=0;j<i;++j) if(classes[i]==classes[j]) return false;
        auto& value=result.items[i];
        if(!ReadWeaponClassName(read,classes[i],value.name) || record[0x6a]>1) return false;
        value.defaultEntry=record[0x6a]!=0;
        const uint16_t mask=uint16_t(bytes[0x1b4]) | uint16_t(bytes[0x1b5])<<8;
        value.nativeEnabled=(mask & (1u<<i))==0;
        for(size_t j=0;j<owned.count;++j) if(owned.values[j]==classes[i]) value.owned=true;
        for(size_t j=0;j<mounted;++j) if(WeaponWord(bytes.data()+0x74+j*8)==classes[i]) {
            if(value.mounted || !WeaponWord(bytes.data()+0x78+j*8)) return false;
            value.mounted=true;
        }
    }
    // Reject mounted records that cannot be joined to this compatibility table.
    for(size_t j=0;j<mounted;++j) {
        bool found=false;
        for(size_t i=0;i<count;++i) if(classes[i]==WeaponWord(bytes.data()+0x74+j*8)) found=true;
        if(!found) return false;
    }
    uint32_t itemAfter{};
    if(!WeaponCopy(read,item,bytesAfter.data(),bytesAfter.size()) || bytes!=bytesAfter ||
       !WeaponCopy(read,shared,headerAfter.data(),headerAfter.size()) || header!=headerAfter ||
       (count && (!WeaponCopy(read,begin,recordsAfter.data(),count*0x74) || records!=recordsAfter)) ||
       !ReadInventoryEntries(read,inv,true,ownedAfter) || owned.count!=ownedAfter.count || owned.values!=ownedAfter.values ||
       !ReadOwnedWeaponName(read,true,actor,owner,system,id,after) ||
       before.entityId!=after.entityId || before.className!=after.className || before.equipped!=after.equipped ||
       !ReadItemPointer(read,system,id,itemAfter) || itemAfter!=item) return false;
    out=result; return true;
}
} // namespace c3::research

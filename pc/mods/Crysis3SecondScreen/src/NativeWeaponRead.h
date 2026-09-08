#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "NativeWeaponProfile.h"

namespace c3::research {
// Read-only research support for the exact captured 1.2.1.1000 image.
// The caller must gate its reader on the executable/function fingerprints and
// game-thread/lifecycle checks. This header does not authorize an adapter/action.
// No native pointers are dereferenced here: Reader copies into owned storage.
inline constexpr uint32_t InventoryVtable = 0x3988c288;
inline constexpr uint32_t ItemSystemVtable = 0x3988cf40;
inline uint32_t WeaponWord(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
inline bool WeaponRange(uint32_t p, size_t n) {
    return p>=0x10000 && n && n<=4096 && uint64_t(p)+n<=0x100000000ull;
}
template<class Reader> bool WeaponCopy(Reader& read,uint32_t p,void* out,size_t n) {
    return WeaponRange(p,n) && read(p,out,n);
}
struct InventoryObservation {
    uint32_t address{},owner{},currentItem{},ammoRoot{};
    uint32_t itemBegin{},itemEnd{},attachmentBegin{},attachmentEnd{};
};
// Item-system +54/37283230. The caller owns the interface's lifetime; consistency
// checks here only reject changed copies. A missing key succeeds with item=0.
template<class Reader> bool ReadItemPointer(Reader& read,uint32_t system,uint32_t id,uint32_t& item) {
    item=0;
    if (!id || !WeaponRange(system,0x194)) return false;
    std::array<uint8_t,4> vt{},root{},again{};
    if (!WeaponCopy(read,system,vt.data(),4) || WeaponWord(vt.data())!=ItemSystemVtable ||
        !WeaponCopy(read,system+0x190,root.data(),4)) return false;
    uint32_t node=WeaponWord(root.data()),candidate=0;
    std::array<uint8_t,24> chosen{};
    std::array<uint32_t,64> seen{};size_t visits=0;
    while(node) {
        if(node==system+0x18c || visits==seen.size())return false;
        for(size_t i=0;i<visits;++i)if(seen[i]==node)return false;
        seen[visits++]=node;
        std::array<uint8_t,24> a{},b{};
        if(!WeaponCopy(read,node,a.data(),a.size()) ||
           !WeaponCopy(read,node,b.data(),b.size()) || a!=b)return false;
        if(WeaponWord(a.data()+0x10)>=id) {
            candidate=node;chosen=a;node=WeaponWord(a.data()+8);
        } else node=WeaponWord(a.data()+0xc);
    }
    if(!WeaponCopy(read,system+0x190,again.data(),4) || again!=root ||
       !WeaponCopy(read,system,again.data(),4) || again!=vt)return false;
    if(candidate && WeaponWord(chosen.data()+0x10)==id) {
        std::array<uint8_t,24> againNode{};
        if(!WeaponCopy(read,candidate,againNode.data(),againNode.size()) || againNode!=chosen)return false;
        item=WeaponWord(chosen.data()+0x14);
        if(item && !WeaponRange(item,4)) {item=0;return false;}
    }
    return true;
}
// Offsets follow live vtable getters, not C++ layout inference:
// +90/+a4 item count/index, +b0/+b8 class count/index, +cc current ID.
template<class Reader> bool ReadInventory(Reader& read,uint32_t address,uint32_t owner,
                                         InventoryObservation& out) {
    out={};
    std::array<uint8_t,0x98> a{},b{};
    if (!owner || !WeaponCopy(read,address,a.data(),a.size()) ||
        !WeaponCopy(read,address,b.data(),b.size()) || a!=b ||
        WeaponWord(a.data())!=InventoryVtable || WeaponWord(a.data()+8)!=owner) return false;
    InventoryObservation v{address,owner,WeaponWord(a.data()+0x8c),WeaponWord(a.data()+0x2c),
        WeaponWord(a.data()+0x10),WeaponWord(a.data()+0x14),
        WeaponWord(a.data()+0x1c),WeaponWord(a.data()+0x20)};
    auto vector=[](uint32_t first,uint32_t last) {
        return first==last || (first>=0x10000 && last>=first && !(first&3) &&
                               !((last-first)&3) && last-first<=256*4);
    };
    if (!vector(v.itemBegin,v.itemEnd) || !vector(v.attachmentBegin,v.attachmentEnd)) return false;
    out=v; return true;
}
struct AmmoObservation {
    uint32_t ammoClass{};
    int32_t count{},capacity{};
    bool found{};
};
struct InventoryEntries {
    std::array<uint32_t,256> values{};
    size_t count{};
};
template<class Reader> bool ReadInventoryEntries(Reader& read,const InventoryObservation& owner,
                                                bool attachmentClasses,InventoryEntries& out) {
    out={};
    const uint32_t begin=attachmentClasses?owner.attachmentBegin:owner.itemBegin;
    const uint32_t end=attachmentClasses?owner.attachmentEnd:owner.itemEnd;
    if (!owner.address || !owner.owner || end<begin || (begin&3) || ((end-begin)&3) ||
        end-begin>256*4) return false;
    const size_t bytes=end-begin;
    std::array<uint8_t,1024> a{},b{};
    if (bytes && (!WeaponCopy(read,begin,a.data(),bytes) ||
                  !WeaponCopy(read,begin,b.data(),bytes) || a!=b)) return false;
    InventoryObservation after{};
    if (!ReadInventory(read,owner.address,owner.owner,after) ||
        after.currentItem!=owner.currentItem || after.itemBegin!=owner.itemBegin ||
        after.itemEnd!=owner.itemEnd || after.attachmentBegin!=owner.attachmentBegin ||
        after.attachmentEnd!=owner.attachmentEnd) return false;
    InventoryEntries v{};v.count=bytes/4;
    for(size_t i=0;i<v.count;++i)v.values[i]=WeaponWord(a.data()+i*4);
    out=v; return true;
}
// Mirrors inventory virtual +100/+108 unsigned-key lower_bound. The original
// returns zero for absent classes; found keeps absence distinct from real zero.
// Fixed visit limit and seen-set reject corrupt/cyclic trees without allocation.
template<class Reader> bool ReadInventoryAmmo(Reader& read,const InventoryObservation& owner,
                                             uint32_t ammoClass,AmmoObservation& out) {
    out={};
    if (!ammoClass || !owner.address || !owner.owner) return false;
    uint32_t node=owner.ammoRoot,candidate=0;
    std::array<uint32_t,64> seen{};
    std::array<uint8_t,32> candidateBytes{};
    size_t visited=0;
    while (node) {
        if (node==owner.address+0x28 || visited==seen.size()) return false;
        for (size_t i=0;i<visited;++i) if (seen[i]==node) return false;
        seen[visited++]=node;
        std::array<uint8_t,32> bytes{},again{};
        if (!WeaponCopy(read,node,bytes.data(),bytes.size()) ||
            !WeaponCopy(read,node,again.data(),again.size()) || bytes!=again) return false;
        const uint32_t key=WeaponWord(bytes.data()+0x10);
        if (key>=ammoClass) {
            candidate=node; candidateBytes=bytes; node=WeaponWord(bytes.data()+8);
        } else node=WeaponWord(bytes.data()+0xc);
    }
    // A stable header before/after is not an atomic read or a lifetime guarantee.
    InventoryObservation after{};
    if (!ReadInventory(read,owner.address,owner.owner,after) ||
        after.ammoRoot!=owner.ammoRoot || after.currentItem!=owner.currentItem) return false;
    AmmoObservation v{}; v.ammoClass=ammoClass;
    if (candidate && WeaponWord(candidateBytes.data()+0x10)==ammoClass) {
        std::array<uint8_t,32> again{};
        if (!WeaponCopy(read,candidate,again.data(),again.size()) || again!=candidateBytes) return false;
        const uint32_t count=WeaponWord(candidateBytes.data()+0x14);
        const uint32_t capacity=WeaponWord(candidateBytes.data()+0x1c);
        std::memcpy(&v.count,&count,4); std::memcpy(&v.capacity,&capacity,4);
        v.found=true;
    }
    out=v; return true;
}

struct RegularSingleObservation {
    uint32_t entityId{},ammoClass{};
    int32_t loadedRounds{},clipLimit{},displayRounds{},overflowRounds{};
    std::array<char,64> weaponClassName{},ammoClassName{};
};
template<class Reader,size_t N> bool WeaponStableCopy(Reader& read,uint32_t address,
                                                    std::array<uint8_t,N>& bytes) {
    std::array<uint8_t,N> again{};
    return WeaponCopy(read,address,bytes.data(),N) &&
           WeaponCopy(read,address,again.data(),N) && bytes==again;
}
template<class Reader> bool ReadWeaponClassName(Reader& read,uint32_t address,
                                               std::array<char,64>& out) {
    out={};std::array<uint8_t,12> header{};std::array<uint8_t,64> bytes{};
    if(!WeaponStableCopy(read,address,header) || WeaponWord(header.data())!=0x398f4638 ||
       !WeaponStableCopy(read,WeaponWord(header.data()+8),bytes))return false;
    size_t n=0;while(n<bytes.size() && bytes[n]) {
        if(bytes[n]<0x20 || bytes[n]>0x7e)return false;
        ++n;
    }
    if(!n || n==bytes.size())return false;
    std::array<uint8_t,12> after{};
    if(!WeaponCopy(read,address,after.data(),after.size()) || after!=header)return false;
    std::memcpy(out.data(),bytes.data(),n);return true;
}
struct OwnedWeaponName {
    uint32_t entityId{};
    bool equipped{};
    std::array<char,64> className{};
};
// Generic CItem identity, NOT ammo/mode layout. IDs must belong to this actor's
// actual inventory. Internal class names are not localized display names, and
// inventory order is not asserted to mean primary/secondary equipment slots.
// Caller checks BOTH fingerprint profiles and the same game-thread/lifecycle
// gate as ReadRegularSingle. Failure is unavailable, never a synthetic weapon.
template<class Reader> bool ReadOwnedWeaponName(Reader& read,bool profileVerified,
    uint32_t actor,uint32_t ownerId,uint32_t system,uint32_t id,OwnedWeaponName& out) {
    out={};if(!profileVerified || !ownerId || !id)return false;
    std::array<uint8_t,0x44> actorBytes{};
    if(!WeaponStableCopy(read,actor,actorBytes) || WeaponWord(actorBytes.data())!=0x399b5940 ||
       WeaponWord(actorBytes.data()+8)!=ownerId || actorBytes[0x21]!=1)return false;
    InventoryObservation inv{};InventoryEntries entries{};
    if(!ReadInventory(read,WeaponWord(actorBytes.data()+0x40),ownerId,inv) ||
       !ReadInventoryEntries(read,inv,false,entries))return false;
    size_t matches=0;for(size_t i=0;i<entries.count;++i)if(entries.values[i]==id)++matches;
    if(matches!=1)return false;
    uint32_t item=0;
    if(!ReadItemPointer(read,system,id,item) || !item)return false;
    std::array<uint8_t,0x10c> itemBytes{};
    if(!WeaponStableCopy(read,item,itemBytes) || !IsWeaponNameFamily(WeaponWord(itemBytes.data())) ||
       WeaponWord(itemBytes.data()+8)!=id || WeaponWord(itemBytes.data()+0x100)!=ownerId ||
       WeaponWord(itemBytes.data()+0x104)!=actor)return false;
    const uint32_t weak=WeaponWord(itemBytes.data()+0x108),entity=WeaponWord(itemBytes.data()+0xc);
    std::array<uint8_t,12> weakBytes{};std::array<uint8_t,32> entityBytes{};
    if(!WeaponStableCopy(read,weak,weakBytes) || WeaponWord(weakBytes.data())!=0x399ba1d8 ||
       !WeaponWord(weakBytes.data()+4) || !WeaponStableCopy(read,entity,entityBytes) ||
       WeaponWord(entityBytes.data())!=0x398eccc0 || WeaponWord(entityBytes.data()+0xc)!=id)return false;
    OwnedWeaponName result{};result.entityId=id;result.equipped=inv.currentItem==id;
    if(!ReadWeaponClassName(read,WeaponWord(entityBytes.data()+0x1c),result.className))return false;
    std::array<uint8_t,0x44> actorAfter{};std::array<uint8_t,0x10c> itemAfter{};
    std::array<uint8_t,12> weakAfter{};std::array<uint8_t,32> entityAfter{};
    InventoryEntries entriesAfter{};uint32_t itemAfterLookup=0;
    if(!WeaponCopy(read,actor,actorAfter.data(),actorAfter.size()) || actorAfter!=actorBytes ||
       !WeaponCopy(read,item,itemAfter.data(),itemAfter.size()) || itemAfter!=itemBytes ||
       !WeaponCopy(read,weak,weakAfter.data(),weakAfter.size()) || weakAfter!=weakBytes ||
       !WeaponCopy(read,entity,entityAfter.data(),entityAfter.size()) || entityAfter!=entityBytes ||
       !ReadInventoryEntries(read,inv,false,entriesAfter) || entriesAfter.count!=entries.count ||
       entriesAfter.values!=entries.values || !ReadItemPointer(read,system,id,itemAfterLookup) ||
       itemAfterLookup!=item)return false;
    out=result;return true;
}
struct OwnedWeaponList {
    std::array<OwnedWeaponName,32> weapons{};
    size_t count{},unavailable{};
    uint32_t currentItem{};
};
// Bounded inventory-name enumeration; unavailable counts IDs that are not a
// verified owned weapon family (including ordinary non-weapon inventory items).
// There is intentionally no primary/secondary slot inference. A host should
// sample names at a low cadence, and invalidate the whole result on its epoch.
template<class Reader> bool ReadOwnedWeaponList(Reader& read,bool profileVerified,
    uint32_t actor,uint32_t ownerId,uint32_t system,OwnedWeaponList& out) {
    out={};if(!profileVerified || !ownerId)return false;
    std::array<uint8_t,0x44> actorBytes{};
    if(!WeaponStableCopy(read,actor,actorBytes) || WeaponWord(actorBytes.data())!=0x399b5940 ||
       WeaponWord(actorBytes.data()+8)!=ownerId || actorBytes[0x21]!=1)return false;
    InventoryObservation inv{};InventoryEntries entries{};
    if(!ReadInventory(read,WeaponWord(actorBytes.data()+0x40),ownerId,inv) ||
       !ReadInventoryEntries(read,inv,false,entries) || entries.count>32)return false;
    OwnedWeaponList result{};result.currentItem=inv.currentItem;
    for(size_t i=0;i<entries.count;++i) {
        if(!entries.values[i])return false;
        for(size_t j=0;j<i;++j)if(entries.values[j]==entries.values[i])return false;
        OwnedWeaponName name{};
        if(ReadOwnedWeaponName(read,profileVerified,actor,ownerId,system,entries.values[i],name))
            result.weapons[result.count++]=name;
        else ++result.unavailable;
    }
    std::array<uint8_t,0x44> actorAfter{};InventoryEntries entriesAfter{};
    if(!WeaponCopy(read,actor,actorAfter.data(),actorAfter.size()) || actorAfter!=actorBytes ||
       !ReadInventoryEntries(read,inv,false,entriesAfter) || entriesAfter.count!=entries.count ||
       entriesAfter.values!=entries.values)return false;
    out=result;return true;
}
// Captured Hammer / regular Single path only. Unknown/derived vtables, non-player
// ownership, special magazine behavior, malformed names and negative capacities
// are unavailable, not substituted. Never invokes methods or marks a UI adapter
// verified. profileVerified means the HOST has checked the exact code closure;
// it is not established by names/vtable constants alone. The HOST must also run
// in the verified update phase and invalidate on epoch/actor/selection changes.
template<class Reader> bool ReadRegularSingle(Reader& read,bool profileVerified,
    uint32_t actor,uint32_t ownerId,uint32_t item,RegularSingleObservation& out) {
    out={};
    if(!profileVerified || !ownerId)return false;
    std::array<uint8_t,0x44> actorBytes{};
    std::array<uint8_t,0x2e0> itemBytes{};
    if(!WeaponStableCopy(read,actor,actorBytes) || WeaponWord(actorBytes.data())!=0x399b5940 ||
       WeaponWord(actorBytes.data()+8)!=ownerId || actorBytes[0x21]!=1 ||
       !WeaponStableCopy(read,item,itemBytes) || WeaponWord(itemBytes.data())!=0x399e3d08 ||
       WeaponWord(itemBytes.data()+0x104)!=actor ||
       WeaponWord(itemBytes.data()+0x1bc)!=0x399e3b70)return false;
    const uint32_t id=WeaponWord(itemBytes.data()+8);
    InventoryObservation inventory{};
    if(!ReadInventory(read,WeaponWord(actorBytes.data()+0x40),ownerId,inventory) ||
       !id || inventory.currentItem!=id)return false;
    const uint32_t weak=WeaponWord(itemBytes.data()+0x108);
    std::array<uint8_t,12> weakBytes{};
    if(!WeaponStableCopy(read,weak,weakBytes) || WeaponWord(weakBytes.data())!=0x399ba1d8 ||
       !WeaponWord(weakBytes.data()+4))return false;
    const uint32_t shared=WeaponWord(itemBytes.data()+0x14);
    std::array<uint8_t,1> special{};
    if(!WeaponRange(shared,0x31) || !WeaponStableCopy(read,shared+0x30,special) || special[0])return false;
    const uint32_t modeBegin=WeaponWord(itemBytes.data()+0x210);
    const uint32_t modeEnd=WeaponWord(itemBytes.data()+0x214);
    const uint32_t modeIndex=WeaponWord(itemBytes.data()+0x2dc);
    if(modeEnd<modeBegin || ((modeEnd-modeBegin)&3) || modeEnd-modeBegin>64*4 ||
       (modeBegin&3) || modeIndex>=(modeEnd-modeBegin)/4)return false;
    std::array<uint8_t,4> modePointer{};
    if(!WeaponStableCopy(read,modeBegin+modeIndex*4,modePointer))return false;
    const uint32_t mode=WeaponWord(modePointer.data());
    std::array<uint8_t,12> modeBytes{};
    if(!WeaponStableCopy(read,mode,modeBytes) || WeaponWord(modeBytes.data())!=0x399e6b28 ||
       WeaponWord(modeBytes.data()+4)!=item)return false;
    const uint32_t holder=WeaponWord(modeBytes.data()+8);
    std::array<uint8_t,8> holderBytes{};
    std::array<uint8_t,0x98> parameters{};
    if(!WeaponStableCopy(read,holder,holderBytes) ||
       !WeaponStableCopy(read,WeaponWord(holderBytes.data()+4),parameters))return false;
    const uint32_t ammoClass=WeaponWord(parameters.data()+0x7c);
    const uint16_t clipBits=uint16_t(parameters[0x8a]) | uint16_t(parameters[0x8b])<<8;
    int16_t clip{};std::memcpy(&clip,&clipBits,2);
    if(!ammoClass || clip<=0)return false;
    const uint32_t ammoBegin=WeaponWord(itemBytes.data()+0x238);
    const uint32_t ammoEnd=WeaponWord(itemBytes.data()+0x23c);
    if(ammoEnd<ammoBegin || ((ammoEnd-ammoBegin)&7) || (ammoBegin&3) ||
       ammoEnd-ammoBegin>64*8)return false;
    std::array<uint8_t,512> ammoBytes{},ammoAgain{};
    const size_t ammoSize=ammoEnd-ammoBegin;
    if(ammoSize && (!WeaponCopy(read,ammoBegin,ammoBytes.data(),ammoSize) ||
       !WeaponCopy(read,ammoBegin,ammoAgain.data(),ammoSize) || ammoBytes!=ammoAgain))return false;
    int32_t loaded=0;bool found=false;
    for(size_t i=0;i<ammoSize;i+=8)if(WeaponWord(ammoBytes.data()+i)==ammoClass) {
        if(found)return false;
        const uint32_t value=WeaponWord(ammoBytes.data()+i+4);std::memcpy(&loaded,&value,4);
        found=true;
    }
    // Original getter returns zero for a missing ammo-class entry; keep that
    // exact behavior, but do not interpret negative/debug counts as normal ammo.
    // HUD stores these fields as signed shorts. Out-of-range/debug values are
    // unsupported here, avoiding a misleading reconstruction after truncation.
    if(loaded<0 || loaded>INT16_MAX)return false;
    const uint32_t entity=WeaponWord(itemBytes.data()+0xc);
    std::array<uint8_t,32> entityBytes{};
    if(!WeaponStableCopy(read,entity,entityBytes) || WeaponWord(entityBytes.data())!=0x398eccc0 ||
       WeaponWord(entityBytes.data()+0xc)!=id)return false;
    RegularSingleObservation result{};result.entityId=id;result.ammoClass=ammoClass;
    result.loadedRounds=loaded;result.clipLimit=clip;
    result.displayRounds=loaded<clip?loaded:clip;
    result.overflowRounds=loaded>clip?loaded-clip:0;
    if(!ReadWeaponClassName(read,WeaponWord(entityBytes.data()+0x1c),result.weaponClassName) ||
       !ReadWeaponClassName(read,ammoClass,result.ammoClassName))return false;
    // Recheck ownership/configuration and live values after all bounded reads.
    std::array<uint8_t,0x44> actorAfter{};std::array<uint8_t,0x2e0> itemAfter{};
    std::array<uint8_t,12> weakAfter{},modeAfter{};std::array<uint8_t,8> holderAfter{};
    std::array<uint8_t,0x98> paramsAfter{};std::array<uint8_t,1> specialAfter{};
    std::array<uint8_t,4> modePointerAfter{};std::array<uint8_t,32> entityAfter{};
    InventoryObservation invAfter{};
    if(!WeaponCopy(read,actor,actorAfter.data(),actorAfter.size()) || actorAfter!=actorBytes ||
       !WeaponCopy(read,item,itemAfter.data(),itemAfter.size()) || itemAfter!=itemBytes ||
       !WeaponCopy(read,weak,weakAfter.data(),weakAfter.size()) || weakAfter!=weakBytes ||
       !WeaponCopy(read,mode,modeAfter.data(),modeAfter.size()) || modeAfter!=modeBytes ||
       !WeaponCopy(read,holder,holderAfter.data(),holderAfter.size()) || holderAfter!=holderBytes ||
       !WeaponCopy(read,WeaponWord(holderBytes.data()+4),paramsAfter.data(),paramsAfter.size()) || paramsAfter!=parameters ||
       !WeaponCopy(read,shared+0x30,specialAfter.data(),1) || specialAfter!=special ||
       !WeaponCopy(read,modeBegin+modeIndex*4,modePointerAfter.data(),4) || modePointerAfter!=modePointer ||
       !WeaponCopy(read,entity,entityAfter.data(),entityAfter.size()) || entityAfter!=entityBytes ||
       !ReadInventory(read,inventory.address,ownerId,invAfter) || invAfter.currentItem!=id ||
       (ammoSize && (!WeaponCopy(read,ammoBegin,ammoAgain.data(),ammoSize) || ammoAgain!=ammoBytes)))return false;
    out=result;return true;
}
}

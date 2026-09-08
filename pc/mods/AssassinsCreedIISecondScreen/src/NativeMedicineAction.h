#pragma once
#include "NativeHealth.h"
#include "NativeInventoryMenu.h"
#include "NativeEventSubscriber.h"

namespace ac2ds::native {
struct MedicineTarget {
    uint32_t entity{}, subscriber{}, service{}, definition{}, container{}, quantity{}, health{}, maximum{};
    bool operator==(const MedicineTarget& other)const {
        return entity==other.entity && subscriber==other.subscriber && service==other.service &&
            definition==other.definition && container==other.container && quantity==other.quantity &&
            health==other.health && maximum==other.maximum;
    }
};
// 011063e0's SelectWeapon(8) sends 010d7830(1). 015f21b0 queues the
// request at subscriber+1c; 017be610 consumes it, runs native availability /
// healing checks and clears it with 010d7830(0). Never write health or stock.
// Utility5 -> category7 is 010dd060's mapping; inventory menu14 is medicine.
inline std::optional<MedicineTarget> ResolveMedicineTarget(const Memory& source,const Roots& roots){
    ReadTransaction memory(source);Roots before{};
    if(!ReadRoots(memory,before) || !(before==roots) || !roots.world || !roots.player || !roots.progress)return {};
    uint32_t ui{},type{};uint8_t forced{},overrideLock{},restricted{};
    if(!memory.Read(0x223f820,ui) || !memory.Read(ui,type) || type!=0x1e60898 ||
        !memory.Member(ui,0xfb,forced) || forced || !memory.Member(ui,0xfc,overrideLock) || overrideLock ||
        !memory.Member(ui,0xfe,restricted) || restricted)return {};
    auto player=ResolvePlayerInventory(memory,roots.player);if(!player)return {};
    constexpr std::array<EventDescriptor,3> events{{
        {0x223f730,0x15f21b0},{0x223f708,0x15f27c0},{0x223f728,0x15f2880}}};
    auto weapon=ResolveEventSubscriber(memory,player->context,events,0x1ea12e0,0x1ea1310);
    if(!weapon)return {};
    uint32_t pending{};uint8_t available{};
    if(!memory.Member(weapon->interface,0x1c,pending) || pending ||
        !memory.Member(weapon->owner,0x6c,available) || available!=1)return {};
    // 01106d80 sends LOCK_WEAPON=true when this availability byte is zero.
    // Reject forced UI overrides instead of opening normally locked slots.
    auto definitions=ReadReloadDefinitions(memory);auto inventory=ReadPlayerInventory(memory,roots.player);
    auto health=ReadHealth(memory,roots);
    if(!definitions || !inventory || !health || !health->current ||
        health->current>=health->RestorableMaximum())return {};
    MedicineTarget target{player->entity,weapon->interface,weapon->owner,0,0,0,health->current,health->RestorableMaximum()};
    for(const auto& definition:*definitions)if(definition.menuId==14 && !definition.locked)
        target.definition=definition.identity;
    for(const auto& item:inventory->containers)if(item.category==7 && item.quantity && item.capacity){
        target.container=item.identity;target.quantity=item.quantity;
    }
    if(!target.definition || !target.container || !memory.Validate())return {};
    return target;
}
enum class MedicineActionResult { Unavailable, StateChanged, Dispatched, NativeRejected, Cooldown };
struct MedicineEngine {
    virtual ~MedicineEngine()=default;
    virtual bool Request(const MedicineTarget& target)=0;
};
// A queued request can be cleared before the next UI sample. Limit repeated
// taps even then; this does not replace the native game's own checks.
class MedicineAction {
    uint64_t lastAttempt_{};bool attempted_{};
public:
    MedicineActionResult Use(const Memory& memory,const Roots& roots,uint64_t now,MedicineEngine& engine){
        if(attempted_ && (now<lastAttempt_ || now-lastAttempt_<1000))return MedicineActionResult::Cooldown;
        auto target=ResolveMedicineTarget(memory,roots);if(!target)return MedicineActionResult::Unavailable;
        auto current=ResolveMedicineTarget(memory,roots);
        if(!current || !(*current==*target))return MedicineActionResult::StateChanged;
        lastAttempt_=now;attempted_=true;
        return engine.Request(*current)?MedicineActionResult::Dispatched:MedicineActionResult::NativeRejected;
    }
};
}

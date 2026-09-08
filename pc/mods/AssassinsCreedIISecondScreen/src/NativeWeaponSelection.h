#pragma once
#include "NativeEquipment.h"
#include "NativeHealth.h"
#include "NativeEventSubscriber.h"
namespace ac2ds::native {
struct WeaponSelectionTarget {
    uint32_t entity{},subscriber{},service{},id{},handle{},menu{},primary{};
    bool operator==(const WeaponSelectionTarget& other)const {
        return entity==other.entity && subscriber==other.subscriber && service==other.service &&
            id==other.id && handle==other.handle && menu==other.menu && primary==other.primary;
    }
};
// Native selector1 -> primary1 (010e9830). 010f9750 maps primary1 to
// equipment type5, whose explicit ownership check012d9350 resolves inventory
// group2/menu6. 01942d40's version query012d93f0 uses menu7 for double blades.
// Both select the native hidden-blade slot; they are not separate weapons.
// SelectWeapon selectors4/5 map to primary2/4. 010f9750 resolves those to
// equipment subtypes1/2 (carried long/small weapons, inventory menus5/4).
// Selection is offered only for the currently carried equipped entry; this
// request does not swap an arbitrary owned weapon from Villa storage.
inline std::optional<WeaponSelectionTarget> ResolveWeaponSelectionTarget(const Memory& source,const Roots& roots,uint32_t id){
    ReadTransaction memory(source);Roots before{};
    if(!id || !ReadRoots(memory,before) || !(before==roots) || !roots.world || !roots.player || !roots.progress)return {};
    uint32_t ui{},type{};uint8_t forced{},overrideLock{},restricted{};
    if(!memory.Read(0x223f820,ui) || !memory.Read(ui,type) || type!=0x1e60898 ||
        !memory.Member(ui,0xfb,forced) || forced || !memory.Member(ui,0xfc,overrideLock) || overrideLock ||
        !memory.Member(ui,0xfe,restricted) || restricted)return {};
    auto player=ResolvePlayerInventory(memory,roots.player);if(!player)return {};
    constexpr std::array<EventDescriptor,3> events{{
        {0x223f72c,0x15f28a0},{0x223f700,0x15f2780},{0x223f728,0x15f2880}}};
    auto weapon=ResolveEventSubscriber(memory,player->context,events,0x1ea12e0,0x1ea1310);
    if(!weapon)return {};
    uint32_t pending{};
    // Original011063e0 only sends the request if010d7350 returns6 (idle).
    if(!memory.Member(weapon->owner,0x5c,pending) || pending!=6)return {};
    auto health=ReadHealth(memory,roots);if(!health || !health->current)return {};
    auto equipment=ReadEquipment(memory,roots.player);if(!equipment)return {};
    std::optional<WeaponSelectionTarget> target;
    for(const auto& item:*equipment)if(item.id==id){
        if(target || !item.definitionHandle)return {};
        uint32_t primary=item.menu==6 || item.menu==7?1:item.menu==5?2:item.menu==4?4:6;
        if(primary==6)return {};
        if(primary!=1){
            uint32_t definition{},category{},subtype{};
            if(!item.equipped || !memory.Reference(item.definitionHandle,definition) ||
                !memory.Member(definition,0xc,category) || category!=2 ||
                !memory.Member(definition,0x10,subtype) || subtype!=(primary==2?1u:2u))return {};
        }
        uint8_t available{};
        if(!memory.Member(weapon->owner,0x60+primary,available) || available!=1)return {};
        target=WeaponSelectionTarget{player->entity,weapon->interface,weapon->owner,id,item.definitionHandle,item.menu,primary};
    }
    if(!target || !memory.Validate())return {};
    return target;
}
enum class WeaponSelectionResult { Unavailable, StateChanged, Dispatched, NativeRejected, Cooldown };
struct WeaponSelectionEngine {
    virtual ~WeaponSelectionEngine()=default;
    virtual bool Request(const WeaponSelectionTarget& target)=0;
};
class WeaponSelectionAction {
    uint64_t lastAttempt_{};bool attempted_{};
public:
    WeaponSelectionResult Select(const Memory& memory,const Roots& roots,uint32_t id,uint64_t now,WeaponSelectionEngine& engine){
        if(attempted_ && (now<lastAttempt_ || now-lastAttempt_<500))return WeaponSelectionResult::Cooldown;
        auto target=ResolveWeaponSelectionTarget(memory,roots,id);if(!target)return WeaponSelectionResult::Unavailable;
        auto current=ResolveWeaponSelectionTarget(memory,roots,id);
        if(!current || !(*current==*target))return WeaponSelectionResult::StateChanged;
        lastAttempt_=now;attempted_=true;
        return engine.Request(*current)?WeaponSelectionResult::Dispatched:WeaponSelectionResult::NativeRejected;
    }
};
static_assert(sizeof(void*)==4,"AC II event bridge requires x86");
using PrimaryRequestCall=uint32_t(__attribute__((fastcall)) *)(uint32_t*,void*,uint32_t);
class WeaponSelectionEventEngine final : public WeaponSelectionEngine {
    PrimaryRequestCall request_;
public:
    explicit WeaponSelectionEventEngine(PrimaryRequestCall request):request_(request){}
    bool Request(const WeaponSelectionTarget& target)override {
        uint32_t entity=target.entity;
        return request_ && (target.primary==1 || target.primary==2 || target.primary==4) &&
            request_(&entity,nullptr,target.primary)==0;
    }
};
}

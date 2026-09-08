#pragma once
#include "Model.h"
#include "NativeActorRead.h"
#include "NativeWeaponRead.h"
#include "NativeSuitRead.h"
#include "NativeWeaponCatalog.h"
#include "NativeAttachmentRead.h"
#include <cstdio>

namespace c3 {
inline void TrialVitals(Snapshot& out,const c3native::NativeActorValues& actor,
                        const c3native::NativePhaseValues& phase,bool consistent,
                        uint64_t observationEpoch,uint64_t sequence,uint64_t sampledAt) {
    out={}; out.readOnlyTrial=true; out.generation=observationEpoch;
    out.sequence=sequence; out.sampledAt=sampledAt;
    if(!consistent || !phase.contextPresent || !phase.contextReady || !phase.environmentActive ||
       !phase.pauseKnown || !phase.cutsceneKnown) return;
    out.phase=phase.cutscene?Phase::Cutscene:actor.health<=0?Phase::Dead:phase.paused?Phase::Paused:Phase::Gameplay;
    if(out.phase!=Phase::Gameplay && out.phase!=Phase::Paused) return;
    out.health={actor.health,true}; out.maxHealth={actor.maxHealth,true};
    if(actor.hasSuit) {
        out.energy={actor.energy,true}; out.maxEnergy={actor.maxEnergy,true};
        out.armor={actor.mode==1,true}; out.cloak={actor.mode==2,true};
    }
    // No adapterVerified or capabilities; CanAct rejects readOnlyTrial even
    // if a future caller accidentally adds a verified capability.
}
inline void TrialOwnedWeapons(Snapshot& out,const research::OwnedWeaponList& actual) {
    if(!out.readOnlyTrial || actual.count>actual.weapons.size()) return;
    out.weapons={}; out.weapons.known=true;
    out.weapons.complete=!actual.unavailable && actual.count<=out.weapons.entries.size();
    // Item XML classification is presentation metadata, never ownership. The
    // native inventory also includes utility/interaction CWeapon objects.
    // Keep real primary/secondary categories ahead of the bow and explosives;
    // merely changing the held item must not replace the second loadout row.
    const auto rank=[](const weapon_catalog::Entry* item) {
        if(!item) return 4;
        if(item->category=="utility" || item->selectable==0) return -1;
        if(item->category=="primary") return 0;
        if(item->category=="secondary") return 1;
        if(item->category=="special") return 2;
        return 3;
    };
    for(int group=0;group<5;++group) for(size_t i=0;i<actual.count;++i) {
        const auto& source=actual.weapons[i];
        const auto* metadata=weapon_catalog::Find(source.className.data());
        if(rank(metadata)!=group || out.weapons.count==out.weapons.entries.size()) continue;
        auto& target=out.weapons.entries[out.weapons.count++]; target.key=source.entityId;
        std::snprintf(target.name.bytes.data(),target.name.bytes.size(),"%s",source.className.data());
        target.icon=target.name;
        if(metadata && !metadata->category.empty())
            std::snprintf(target.category.bytes.data(),target.category.bytes.size(),"%.*s",int(metadata->category.size()),metadata->category.data());
        else std::snprintf(target.category.bytes.data(),target.category.bytes.size(),"OWNED WEAPON");
        target.equipped={source.equipped,true};
    }
}
inline void TrialAttachments(Snapshot& out,const research::NativeAttachmentValues& actual) {
    if(!out.readOnlyTrial || !actual.weaponId || actual.count>actual.items.size()) return;
    for(size_t i=0;i<out.weapons.size();++i) {
        auto& weapon=out.weapons.entries[i];
        if(weapon.key!=actual.weaponId) continue;
        weapon.attachments={}; weapon.attachments.known=weapon.attachments.complete=true;
        for(size_t j=0;j<actual.count;++j) {
            const auto& source=actual.items[j];
            auto& target=weapon.attachments.entries[weapon.attachments.count++];
            std::snprintf(target.name.bytes.data(),target.name.bytes.size(),"%s",source.name.data());
            target.equipped={source.mounted,true};
            target.owned={source.owned,true}; target.nativeEnabled={source.nativeEnabled,true};
            target.defaultEntry={source.defaultEntry,true};
            // Ownership and mask are observations, NOT permission to call an ABI.
            // Keep capabilities empty until the command route is verified.
        }
        return;
    }
}
inline void TrialWeapon(Snapshot& out,const research::RegularSingleObservation& actual) {
    if(!out.readOnlyTrial || !actual.entityId) return;
    size_t index=0;
    for(;index<out.weapons.size();++index) if(out.weapons.entries[index].key==actual.entityId) break;
    if(index==out.weapons.size()) {
        if(index==out.weapons.entries.size()) return;
        out.weapons.known=true; out.weapons.complete=false; ++out.weapons.count;
    }
    auto& weapon=out.weapons.entries[index]; weapon.key=actual.entityId;
    std::snprintf(weapon.name.bytes.data(),weapon.name.bytes.size(),"%s",actual.weaponClassName.data());
    weapon.icon=weapon.name;
    std::snprintf(weapon.category.bytes.data(),weapon.category.bytes.size(),
        "%s / CLIP LIMIT %ld / EXTRA LOADED %ld",actual.ammoClassName.data(),
        static_cast<long>(actual.clipLimit),static_cast<long>(actual.overflowRounds));
    weapon.magazine={actual.loadedRounds,true}; weapon.capacity={actual.clipLimit,true};
    weapon.equipped={true,true};
}
inline const char* TrialModuleName(const char* id) {
    // Exact ui_module_<ID> English entries in text_ui_messages.xml.
    // SHA256 608a7037a7f6e11feb76b29970365b2e425bdd93bbed0aeee9678063a651d96e.
    constexpr const char* names[][2]={
        {"StrengthModule","Super Strength"},{"BruteForce","Brute Force"},
        {"LightArmor","Light Armor"},{"EODPackage","Threat Detector"},
        {"HeavyArmor","Heavy Armor"},{"MedicalMonitor","First Aid"},
        {"ExtendedCloak","Extended Cloak"},{"ReflexBoost","Reflex Boost"},
        {"StealthKill","Stealth Kill"},{"Verticality","Verticality"},
        {"Endurance","Endurance"},{"SuperStealth","Super Stealth"},
        {"WeaponDrill","Weapon Handling"},{"EnergyUpgrade","Energy Upgrade"},
        {"BulletProof","Deflection"},{"SensorPackage","Sensor Upgrade"}};
    for(const auto& entry:names) if(!std::strcmp(id,entry[0])) return entry[1];
    return id;
}
inline void TrialSuitModules(Snapshot& out,const c3native::NativeSuitModules& actual) {
    if(!out.readOnlyTrial || actual.count>actual.modules.size()) return;
    out.upgradePoints={actual.upgradePoints,true};
    out.modules={}; out.modules.known=true; out.modules.complete=true;
    for(bool installed:{true,false}) for(size_t i=0;i<actual.count;++i) {
        const auto& source=actual.modules[i]; if(source.installed!=installed) continue;
        auto& target=out.modules.entries[out.modules.count++]; target.key=i+1;
        std::snprintf(target.name.bytes.data(),target.name.bytes.size(),"%s",TrialModuleName(source.id.data()));
        std::snprintf(target.icon.bytes.data(),target.icon.bytes.size(),"%s",source.id.data());
        std::snprintf(target.category.bytes.data(),target.category.bytes.size(),"%s",source.category.data());
        std::snprintf(target.description.bytes.data(),target.description.bytes.size(),"%s%s",
            source.moduleClass.data(),source.maximum?" / MAXIMUM":"");
        target.cost={static_cast<int32_t>(source.cost),true}; target.installed={source.installed,true};
        target.purchased={source.purchased,true}; target.maximum={source.maximum,true};
        target.unlocked={source.unlocked,source.unlockedKnown};
    }
}
inline void TrialSuitResource(Snapshot& out,const c3native::NativeSuitModules& actual) {
    if(!out.readOnlyTrial || actual.upgradePoints<0 || out.resources.count==out.resources.entries.size()) return;
    out.resources.known=true; out.resources.complete=false;
    auto& item=out.resources.entries[out.resources.count++]; item.key=2;
    std::snprintf(item.name.bytes.data(),item.name.bytes.size(),"Nanosuit upgrade points");
    std::snprintf(item.category.bytes.data(),item.category.bytes.size(),"NANOSUIT");
    item.quantity={actual.upgradePoints,true};
}
inline void TrialInventoryAmmo(Snapshot& out,const research::RegularSingleObservation& weapon,
                              const research::AmmoObservation& ammo) {
    if(!out.readOnlyTrial || !ammo.found || !weapon.ammoClass || ammo.ammoClass!=weapon.ammoClass || ammo.count<0 || ammo.capacity<0) return;
    out.resources.known=true; out.resources.complete=false; out.resources.count=1;
    auto& resource=out.resources.entries[0]; resource.key=1;
    std::snprintf(resource.name.bytes.data(),resource.name.bytes.size(),"%s",weapon.ammoClassName.data());
    std::snprintf(resource.category.bytes.data(),resource.category.bytes.size(),"INVENTORY AMMUNITION / ACTIVE WEAPON");
    resource.quantity={ammo.count,true}; resource.capacity={ammo.capacity,true};
    // No weapon.reserve assignment: native HUD reserve has additional branches.
}
} // namespace c3

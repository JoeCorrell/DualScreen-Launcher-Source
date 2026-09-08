#pragma once
#include "Bridge.h"

namespace dishonored {
// A new snapshot is not necessarily a new image. Ignore polling sequence and
// changes to pages which are not visible; retain every shared header/footer value.
inline uint64_t VisibleState(const bridge::Snapshot& state, bridge::Page page, bool fresh, uint64_t now) noexcept {
    uint64_t hash=14695981039346656037ull;
    const auto bytes=[&](const auto& value) {
        const auto* data=reinterpret_cast<const uint8_t*>(&value);
        for (size_t i=0;i<sizeof(value);++i) hash=(hash^data[i])*1099511628211ull;
    };
    const auto value=[&](const auto& item) {bytes(item.known);bytes(item.value);};
    const auto text=[&](bridge::TextRef ref) {
        bytes(ref.length);
        if(bridge::ValidText(state,ref))for(uint32_t i=0;i<ref.length;++i)bytes(state.text[ref.offset+i]);
    };
    const bridge::Domain* domains[]={&state.map.domain,&state.powers.domain,&state.equipment.domain,
        &state.objectives.domain,&state.journal.domain,&state.charms.domain,&state.statistics.domain};
    const auto& domain=*domains[uint32_t(page)];
    bytes(page);bytes(state.generation);bytes(state.lifecycle);bytes(fresh);
    bytes(domain.revision);bytes(domain.availability);bytes(domain.stored);bytes(domain.total);
    value(state.health);value(state.maximumHealth);value(state.mana);value(state.maximumMana);
    if(page==bridge::Page::Map || page==bridge::Page::Objectives)
        for(uint32_t i=0;i<state.objectiveTargets.domain.stored && i<state.objectiveTargets.entries.size();++i) {
            const auto at=state.objectiveTargets.entries[i].observedAt;
            bytes(at && at<=now && now-at<=bridge::FreshnessMilliseconds);
        }
    if(page==bridge::Page::Map) {
        bytes(state.map.key);bytes(state.map.followingPlayer);value(state.map.acquired);
        value(state.map.playerX);value(state.map.playerY);value(state.map.facingRadians);
        bytes(state.map.positionApproximate);
        for(uint32_t i=0;i<state.map.sheets.domain.stored && i<state.map.sheets.entries.size();++i) {
            const auto& sheet=state.map.sheets.entries[i];
            value(sheet.playerX);value(sheet.playerY);value(sheet.facingRadians);bytes(sheet.positionApproximate);
        }
        bytes(state.objectiveTargets.domain.availability);bytes(state.objectiveTargets.domain.stored);
        for(uint32_t i=0;i<state.objectiveTargets.domain.stored && i<state.objectiveTargets.entries.size();++i) {
            const auto& target=state.objectiveTargets.entries[i];
            bytes(target.task);bytes(target.optional);value(target.worldX);value(target.worldY);value(target.worldZ);text(target.label.title);
        }
    }
    if(page==bridge::Page::Statistics) {value(state.coins);value(state.runes);value(state.chaos);}
    if(page==bridge::Page::BoneCharms) {value(state.charmSlots);for(const auto& slot:state.equippedCharms)value(slot);}
    if(page==bridge::Page::Objectives) {
        text(state.chapter.title);text(state.chapter.description);
        for(const auto& target:state.chapterTargets) {value(target.state);text(target.label.title);text(target.label.description);bytes(target.label.artwork.id);}
        bytes(state.objectiveTargets.domain.revision);bytes(state.objectiveTargets.domain.availability);
    }
    return hash;
}
}

#pragma once
#include "NativeInventory.h"
#include "NativeDnaCondition.h"
#include "NativeReadTransaction.h"
#include <array>

namespace ac2ds::native {
struct DnaMemory {
    uint32_t sequence{}, index{}, title{}, description{}, sequenceTitle{}, state{}, synchronization{};
    bool active{};
};
// Read the proven branches of 0126ea10. State 1 delegates to a polymorphic
// mission condition (005f7aa0); verified condition/mode branches are read below,
// while unresolved evaluators remain unexposed rather than treated as unlocked.
inline std::optional<uint32_t> DnaStatus(uint32_t nativeState, uint32_t overrideState,
        uint8_t located, uint8_t forcedAvailable, uint32_t condition, uint8_t specialMode,std::optional<bool> availability={}) {
    if (nativeState > 6) return {};
    if (overrideState == 1) return 2;
    if (nativeState == 1) {
        if(availability)return *availability?(located?2u:1u):0u;
        if ((!forcedAvailable && condition) || specialMode) return {};
        return located ? 2 : 1;
    }
    if (nativeState == 3) return located ? 2 : 1;
    constexpr uint32_t conversion[]{0, 0, 0, 1, 0, 2, 3};
    return conversion[nativeState];
}
inline std::optional<std::vector<DnaMemory>> ReadDna(const Memory& source) {
    ReadTransaction memory(source);
    uint32_t manager{}, catalog{}, catalogSlot{}, configuration{}; uint8_t mode{};
    uint32_t dlcConfiguration{},dlcMask{};bool readEntitlements{};
    PointerArray catalogs{}, sequences{};
    if (!memory.Read(0x0223f81c, manager) || !ArrayHeader(memory, manager, 0xc, 64, catalogs) ||
        !memory.Read(0x022128bc, configuration) || !memory.Member(configuration, 0x5d, mode)) return {};
    // 01596f00 -> 0108bbb0 selects this exact catalog through type metadata.
    for (uint32_t i = 0; i < catalogs.Count(); ++i) {
        uint32_t candidate{}, type{};
        if (!memory.Field(catalogs.data, i * 4, candidate)) return {};
        if (!candidate) continue;
        if (!memory.Field(candidate, 0, type)) return {};
        if (type == 0x01e75650) {
            if (catalog) return {};
            catalog = candidate; catalogSlot = catalogs.data + i * 4;
        }
    }
    if (!catalog || !ArrayHeader(memory, catalog, 8, 32, sequences)) return {};
    std::vector<DnaMemory> result;
    for (uint32_t s = 0; s < sequences.Count(); ++s) {
        uint32_t handle{}, sequence{}, phaseHandle{}, phase{};
        std::array<uint32_t, 11> row{};
        if (!memory.Field(sequences.data, s * 4, handle) || !memory.Reference(handle, sequence)) return {};
        if (!sequence) continue;
        if (!memory.Read(sequence, row) || row[0] != 0x01e76a60) return {};
        // Positive native visibility branch of 012ae520 / 0114c970: an
        // entitled sequence may expose its normally discovered memories.
        // Unentitled DLC's separate GUID 20204d58 placeholder fallback is
        // still omitted; it must never unlock the DLC's own memories.
        if(row[2]){
            if(!readEntitlements){
                if(!memory.Read(0x22128b4,dlcConfiguration) || !memory.Member(dlcConfiguration,0x2c0,dlcMask))return {};
                readEntitlements=true;
            }
            if(!(row[2]&dlcMask))continue;
        }
        phaseHandle = row[6]; uint32_t phaseType{}, phaseValue{};
        if (!memory.Reference(phaseHandle, phase)) return {};
        if (!phase) continue;
        if (!memory.Field(phase, 0, phaseType) || phaseType != 0x01cb8d38 ||
            !memory.Field(phase, 0x20, phaseValue)) return {};
        if (phaseValue == 1) continue; // 012ae520 excludes this native sequence state.
        PointerArray memories{};
        if (!ArrayHeader(memory, sequence, 0x24, 64, memories)) return {};
        bool visible = false; std::vector<DnaMemory> entries;
        for (uint32_t m = 0; m < memories.Count(); ++m) {
            uint32_t reference{}, entry{}, state{};
            std::array<uint32_t, 24> definition{};
            if (!memory.Field(memories.data, m * 4, reference) || !memory.Reference(reference, entry)) return {};
            if (!entry) continue;
            if (!memory.Read(entry, definition) || definition[0] != 0x01e76a00 ||
                !memory.Reference(definition[15], state)) return {};
            if (!state) continue;
            uint32_t stateType{}, value{}, synch{}, condition{}; uint8_t located{}, available{};
            if (!memory.Field(state, 0, stateType) || stateType != 0x01cb8f58 ||
                !memory.Field(state, 0x18, value) || !memory.Field(state, 0x1c, synch) ||
                !memory.Field(state, 0x20, condition) || !memory.Member(state, 0x94, located) ||
                !memory.Member(state, 0x9a, available) || synch > 100) return {};
            auto permission=value==1 && definition[2]!=1?DnaAvailability(memory,state,configuration,mode,condition,available):std::optional<bool>{};
            auto status = DnaStatus(value, definition[2], located, available, condition, mode,permission);
            if (status && *status) {
                visible = true;
                // Status 1 has no discovered title/description in the original
                // DNA interface. It remains a numbered placeholder.
                entries.push_back({s, m, *status >= 2 ? definition[3] : 0,
                    *status >= 2 ? definition[21] : 0, row[3], *status, synch, value == 5});
            }
            uint32_t check{}, stateCheck{}, valueCheck{}, synchCheck{};
            std::array<uint32_t, 24> definitionAfter{};
            uint8_t locatedAfter{}, availableAfter{};
            if (!memory.Field(memories.data, m * 4, check) || check != reference ||
                !memory.Reference(reference, check) || check != entry ||
                !memory.Read(entry, definitionAfter) || definitionAfter != definition ||
                !memory.Reference(definition[15], stateCheck) || stateCheck != state ||
                !memory.Field(state, 0x18, valueCheck) || valueCheck != value ||
                !memory.Field(state, 0x1c, synchCheck) || synchCheck != synch ||
                !memory.Field(state, 0x20, check) || check != condition ||
                !memory.Member(state, 0x94, locatedAfter) || locatedAfter != located ||
                !memory.Member(state, 0x9a, availableAfter) || availableAfter != available) return {};
        }
        uint32_t check{}; std::array<uint32_t, 11> rowAfter{};
        if (!SameArray(memory, sequence, 0x24, memories) ||
            !memory.Read(sequence, rowAfter) || rowAfter != row ||
            !memory.Field(sequences.data, s * 4, check) || check != handle ||
            !memory.Reference(handle, check) || check != sequence ||
            !memory.Reference(phaseHandle, check) || check != phase ||
            !memory.Field(phase, 0x20, check) || check != phaseValue) return {};
        if (visible) result.insert(result.end(), entries.begin(), entries.end());
    }
    uint32_t check{}; uint8_t modeAfter{};
    if(readEntitlements && (!memory.Read(0x22128b4,check) || check!=dlcConfiguration ||
        !memory.Member(dlcConfiguration,0x2c0,check) || check!=dlcMask))return {};
    if (!SameArray(memory, catalog, 8, sequences) || !SameArray(memory, manager, 0xc, catalogs) ||
        !memory.Read(catalogSlot, check) || check != catalog || !memory.Read(0x0223f81c, check) || check != manager ||
        !memory.Read(0x022128bc, check) || check != configuration ||
        !memory.Member(configuration, 0x5d, modeAfter) || modeAfter != mode) return {};
    if(!memory.Validate())return {};
    return result;
}
}

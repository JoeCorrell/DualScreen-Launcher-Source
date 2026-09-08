#pragma once
#include "Model.h"
#include <cstring>

namespace c3 {
// Value-only decoding of the observed 2026-09-07 HUD publisher. This does not
// install a hook, walk native pointers, authorize actions, or verify an adapter.
// See NATIVE-RUNTIME.md for source addresses and remaining lifecycle gates.
enum class ObservedSuitMode : int32_t { Tactical=0, Armor=1, Stealth=2 };
struct ObservedEnergy { float value{}; int32_t channel{}; };

inline bool DecodeEnergyEvent(const uint8_t* copy,size_t bytes,ObservedEnergy& out) {
    out={};
    // Caller must supply an owned copy of the pooled payload, not a game pointer.
    // Publisher emits exactly two 8-byte entries after the 8-byte pool header.
    if (!copy || bytes!=24) return false;
    uint32_t count{}; std::memcpy(&count,copy+4,4);
    // Only the LOW BYTE of each type word is initialized by the game.
    if (count!=2 || copy[12]!=2 || copy[20]!=1) return false;
    ObservedEnergy decoded{};
    std::memcpy(&decoded.value,copy+8,4);
    std::memcpy(&decoded.channel,copy+16,4);
    if (!std::isfinite(decoded.value) || decoded.value<0 ||
        decoded.channel<0 || decoded.channel>1) return false;
    out=decoded; return true;
}

struct ObservedVitals {
    std::array<Number,2> energy{};
    Value<ObservedSuitMode> mode{};
};
class VitalsCapture {
    uint64_t generation_{}, sequence_{}, lastAt_{};
    std::array<uint64_t,2> energyAt_{};
    uint64_t modeAt_{};
    ObservedVitals values_{};
    bool Accept(uint64_t generation,uint64_t sequence,uint64_t now) {
        if (!generation_ || generation!=generation_ || !sequence || sequence<=sequence_ ||
            !now || now<lastAt_) return false;
        sequence_=sequence; lastAt_=now; return true;
    }
    static bool Fresh(uint64_t at,uint64_t now) {
        return at && now>=at && now-at<=MaxAgeMs;
    }
public:
    void Invalidate(uint64_t generation) {
        generation_=generation; sequence_=lastAt_=modeAt_=0;
        energyAt_={}; values_={};
    }
    bool Energy(uint64_t generation,uint64_t sequence,uint64_t now,const uint8_t* copy,size_t bytes) {
        if (!Accept(generation,sequence,now)) return false;
        ObservedEnergy decoded{};
        if (!DecodeEnergyEvent(copy,bytes,decoded)) {
            // Malformed channel cannot identify which cached reading to retain.
            values_.energy={}; energyAt_={}; return false;
        }
        values_.energy[size_t(decoded.channel)]={decoded.value,true};
        energyAt_[size_t(decoded.channel)]=now; return true;
    }
    bool Mode(uint64_t generation,uint64_t sequence,uint64_t now,int32_t mode) {
        if (!Accept(generation,sequence,now)) return false;
        if (mode<0 || mode>2) { values_.mode={}; modeAt_=0; return false; }
        values_.mode={static_cast<ObservedSuitMode>(mode),true}; modeAt_=now; return true;
    }
    ObservedVitals Read(uint64_t generation,uint64_t now) const {
        if (!generation || generation!=generation_) return {};
        auto result=values_;
        for (size_t i=0;i<2;++i) if (!Fresh(energyAt_[i],now)) result.energy[i]={};
        if (!Fresh(modeAt_,now)) result.mode={};
        // No inferred maximum, actor identity, phase, eligibility or capabilities.
        // An unchanged event must not be refreshed as new telemetry by the UI.
        return result;
    }
};
} // namespace c3

#pragma once
#include "Model.h"
#include <cstring>
#include <string_view>

namespace c3 {
// Captures the clearPerks/addPerk/updatePerks transaction observed in the
// decompiled Menus_SuitCustomization.gfx. This is NOT a native hooking ABI.
// Native argument marshaling, movie identity and actor ownership remain gated
// on runtime verification. No native actions are inferred from UI flags.
class SuitCapture {
    List<Module,16> pending_{}, committed_{};
    uint64_t generation_{}, committedAt_{};
    bool building_{}, valid_{};
    static bool Copy(Text& out,std::string_view text) {
        if (text.size()>=out.bytes.size() || text.find('\0')!=std::string_view::npos) return false;
        out={}; std::memcpy(out.bytes.data(),text.data(),text.size()); return true;
    }
public:
    void Invalidate(uint64_t generation) {
        generation_=generation; committedAt_=0; pending_={}; committed_={}; building_=valid_=false;
    }
    void ClearPerks(uint64_t generation) {
        Invalidate(generation); building_=valid_=generation!=0;
    }
    // Key is adapter-owned and generation-scoped, not the raw game's perk ID.
    bool AddPerk(uint64_t generation,Key key,std::string_view category,std::string_view displayName,
        bool purchased,bool unlocked,bool active,std::string_view moduleClass,bool maximum,
        std::string_view detail,int32_t cost) {
        (void)category; // Raw grouping remains native-side; not a fabricated suit mode.
        if (!building_ || !valid_ || generation!=generation_ || !key || cost<0 ||
            pending_.count>=pending_.entries.size()) { valid_=false; return false; }
        for (size_t i=0;i<pending_.count;++i) if (pending_.entries[i].key==key) { valid_=false; return false; }
        Module module{}; module.key=key;
        if (!Copy(module.name,displayName) || !Copy(module.description,detail) || !Copy(module.category,moduleClass)) {
            valid_=false; return false;
        }
        module.purchased={purchased,true}; module.installed={active,true}; module.maximum={maximum,true};
        module.unlocked={unlocked,true}; module.cost={cost,true};
        // Deliberately no progress percentage from `maximum`, no eligibility
        // from `unlocked`, and no native command pointers from a Flash string.
        pending_.entries[pending_.count++]=module; return true;
    }
    bool UpdatePerks(uint64_t generation,uint64_t now) {
        if (!building_ || !valid_ || generation!=generation_ || !now) {
            committed_={}; building_=false; return false;
        }
        pending_.known=pending_.complete=true; committed_=pending_; committedAt_=now;
        building_=false; return true;
    }
    List<Module,16> Read(uint64_t generation,uint64_t now) const {
        if (building_ || generation!=generation_ || !committedAt_ || now<committedAt_ || now-committedAt_>MaxAgeMs) return {};
        return committed_;
    }
};
} // namespace c3

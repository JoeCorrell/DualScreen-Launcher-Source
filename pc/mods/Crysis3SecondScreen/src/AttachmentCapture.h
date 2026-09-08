#pragma once
#include "Model.h"
#include <cstring>
#include <string_view>

namespace c3 {
// Value-only transcription of the patched weapon movie's clear/add/mounted/
// update sequence. NOT a native ABI, native inventory reader, or action grant.
// Raw Flash IDs are resolved to unique, generation-scoped keys by the future
// adapter. Movie identity, active weapon ownership and marshaling need runtime proof.
struct CapturedAttachment {
    Key key{}, group{};
    Text name{}, icon{}, detail{};
    Integer ammoCount{}; // Movie argument only; not assumed to be weapon reserve.
    Value<bool> available{};
    bool defaultEntry{}, movieAttached{};
};
struct CapturedAttachments {
    Key weapon{};
    List<CapturedAttachment,12> items{};
};
class AttachmentCapture {
    CapturedAttachments pending_{}, committed_{};
    uint64_t generation_{}, committedAt_{};
    bool building_{}, valid_{};
    static bool Copy(Text& out,std::string_view text) {
        if (text.size()>=out.bytes.size() || text.find('\0')!=std::string_view::npos) return false;
        std::memcpy(out.bytes.data(),text.data(),text.size()); return true;
    }
public:
    void Invalidate(uint64_t generation) {
        generation_=generation; committedAt_=0; pending_={}; committed_={}; building_=valid_=false;
    }
    void Clear(uint64_t generation,Key weapon) {
        Invalidate(generation); pending_.weapon=weapon; building_=valid_=generation && weapon;
    }
    bool Add(uint64_t generation,Key weapon,Key group,Key key,std::string_view name,
        std::string_view icon,bool available,Integer ammoCount,std::string_view detail,bool defaultEntry=false) {
        if (!building_ || !valid_ || generation!=generation_ || weapon!=pending_.weapon || !group || !key ||
            pending_.items.count>=pending_.items.entries.size()) { valid_=false; return false; }
        for (size_t i=0;i<pending_.items.count;++i) {
            const auto& previous=pending_.items.entries[i];
            if (previous.key==key || (defaultEntry && previous.defaultEntry && previous.group==group)) {
                valid_=false; return false;
            }
        }
        CapturedAttachment item{}; item.key=key; item.group=group; item.defaultEntry=defaultEntry;
        if (!Copy(item.name,name) || !Copy(item.icon,icon) || !Copy(item.detail,detail)) { valid_=false; return false; }
        item.ammoCount=ammoCount;
        // addDefaultAttachment_new never stores its _available argument.
        if (!defaultEntry) item.available={available,true};
        pending_.items.entries[pending_.items.count++]=item; return true;
    }
    bool Mounted(uint64_t generation,Key weapon,Key group,Key key) {
        if (!building_ || !valid_ || generation!=generation_ || weapon!=pending_.weapon) { valid_=false; return false; }
        for (size_t i=0;i<pending_.items.count;++i) {
            auto& item=pending_.items.entries[i];
            // The movie marks matches in the non-default list only. It does
            // not clear other matches or infer that the default is mounted.
            if (!item.defaultEntry && item.group==group && item.key==key) { item.movieAttached=true; return true; }
        }
        // An unresolved native ID is incomplete evidence, even though the
        // Flash implementation would silently leave its array unchanged.
        valid_=false; return false;
    }
    bool Update(uint64_t generation,Key weapon,uint64_t now) {
        if (!building_ || !valid_ || generation!=generation_ || weapon!=pending_.weapon || !now) {
            committed_={}; building_=false; return false;
        }
        pending_.items.known=pending_.items.complete=true; committed_=pending_; committedAt_=now;
        building_=false; return true;
    }
    CapturedAttachments Read(uint64_t generation,Key weapon,uint64_t now) const {
        if (building_ || generation!=generation_ || weapon!=committed_.weapon || !committedAt_ ||
            now<committedAt_ || now-committedAt_>MaxAgeMs) return {};
        return committed_;
    }
};
} // namespace c3

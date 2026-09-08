#pragma once
#include "NativeInventory.h"
#include "Localization.h"
#include <algorithm>
#include <map>

namespace ac2ds::native {
// 004be180 walks 40-byte language records, primary then secondary resource.
// 00612ab0 decodes resource+1c. Cache only owned bytes: UI never reads the game
// allocator and no engine lock, text formatter or allocation is called.
class LocalizationCache {
    struct Source {
        uint32_t handle{}, resource{}, table{}, count{}, blob{}, size{};
        bool operator==(const Source& b) const {
            return handle == b.handle && resource == b.resource && table == b.table &&
                count == b.count && blob == b.blob && size == b.size;
        }
    };
    struct Catalog {
        uint32_t manager{};
        PointerArray records{};
        std::vector<Source> sources;
        bool operator==(const Catalog& b) const {
            return manager == b.manager && records == b.records && sources == b.sources;
        }
    };
    Catalog catalog_;
    bool valid_{};
    std::vector<std::vector<uint8_t>> blobs_;
    std::map<uint32_t, std::optional<std::wstring>> strings_;
    static bool Describe(const Memory& memory, Catalog& result) {
        if (!memory.Read(0x02212914, result.manager) ||
            !ArrayHeader(memory, result.manager, 0xc, 8, result.records) ||
            !result.records.Count() || result.records.data > UINT32_MAX - result.records.Count() * 40) return false;
        uint32_t total{};
        for (uint32_t i = 0; i < result.records.Count(); ++i) {
            for (uint32_t offset : {0x10u, 0x14u}) {
                Source source;
                if (!memory.Field(result.records.data, i * 40 + offset, source.handle)) return false;
                if (source.handle && !memory.Reference(source.handle, source.resource)) return false;
                if (source.resource) {
                    uint32_t type{};
                    if (!memory.Field(source.resource, 0, type) || type != 0x01cacbcc ||
                        !memory.Field(source.resource, 0x10, source.table) ||
                        !memory.Field(source.resource, 0x14, source.count) ||
                        !memory.Field(source.resource, 0x1c, source.blob) ||
                        !memory.Field(source.resource, 0x20, source.size)) return false;
                    // The supported build's loaded languages use compressed
                    // resources. Refuse an untraced mutable override table;
                    // falling through could show the wrong language/text.
                    if (source.count || source.size > 2 * 1024 * 1024 ||
                        (source.size && (source.blob < 0x10000 || source.blob > UINT32_MAX - source.size))) return false;
                    total += source.size;
                    if (total > 8 * 1024 * 1024) return false;
                }
                result.sources.push_back(source);
            }
        }
        return true;
    }
public:
    void Clear() { valid_ = false; catalog_ = {}; blobs_.clear(); strings_.clear(); }
    bool Refresh(const Memory& memory) {
        Catalog next;
        if (!Describe(memory, next)) { Clear(); return false; }
        if (valid_ && next == catalog_) return true;
        Clear();
        std::vector<std::vector<uint8_t>> blobs;
        for (const auto& source : next.sources) {
            std::vector<uint8_t> bytes(source.size);
            for (size_t at = 0; at < bytes.size();) {
                const size_t size = std::min<size_t>(32768, bytes.size() - at);
                if (!memory.Copy(source.blob + uint32_t(at), bytes.data() + at, size)) return false;
                at += size;
            }
            blobs.push_back(std::move(bytes));
        }
        Catalog after;
        if (!Describe(memory, after) || !(after == next)) return false;
        catalog_ = std::move(next); blobs_ = std::move(blobs); valid_ = true;
        return true;
    }
    bool StillCurrent(const Memory& memory) const {
        Catalog now; return valid_ && Describe(memory, now) && now == catalog_;
    }
    std::optional<std::wstring> Lookup(uint32_t id) {
        if (!valid_) return {};
        auto found = strings_.find(id);
        if (found != strings_.end()) return found->second;
        std::optional<std::wstring> text;
        for (const auto& bytes : blobs_) {
            text = DecodeLocalization(bytes, id);
            if (text) break;
        }
        if (strings_.size() < 8192) strings_.emplace(id, text);
        return text;
    }
};
}

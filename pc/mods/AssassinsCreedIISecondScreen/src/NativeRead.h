#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace ac2ds::native {
// Exact-build layouts traced from 00459710, 004654d0, 00559ef0 and
// 01123210. No engine code is invoked and no cached pointer is dereferenced.
struct Memory {
    virtual ~Memory() = default;
    virtual bool Copy(uint32_t address, void* output, size_t bytes) const = 0;
    template<class T> bool Read(uint32_t address, T& value) const {
        return address >= 0x10000 && address <= UINT32_MAX - sizeof(T) && Copy(address, &value, sizeof(T));
    }
    bool Field(uint32_t object, uint32_t offset, uint32_t& value) const {
        return object >= 0x10000 && object <= UINT32_MAX - offset && Read(object + offset, value);
    }
    bool Reference(uint32_t handle, uint32_t& object) const {
        uint32_t words[3]{};
        if (!Read(handle, words)) return false;
        object = words[2] & 0x80000000u ? words[0] : 0;
        return true;
    }
};
struct Roots {
    uint32_t session{}, selection{}, world{}, player{}, progress{};
    bool operator==(const Roots& other) const {
        return session == other.session && selection == other.selection && world == other.world &&
            player == other.player && progress == other.progress;
    }
};
inline bool ReadRoots(const Memory& memory, Roots& roots) {
    roots = {};
    if (!memory.Read(0x02212490, roots.session) || !memory.Read(0x02212570, roots.selection) ||
        !memory.Read(0x0223f814, roots.progress)) return false;
    if (!roots.session) return true;
    uint32_t selected{}, reference{};
    if (!memory.Field(roots.selection, 0xd0, selected)) return false;
    if (selected != UINT32_MAX) {
        if (!memory.Field(roots.selection, 0xc4, reference) || !memory.Reference(reference, roots.world)) return false;
    } else if (!memory.Field(roots.session, 0x4c, roots.world)) return false;
    if (!roots.world) return true;
    uint32_t players{}, array{}, selector{}, index{};
    if (!memory.Field(roots.world, 0x53c, players) || !memory.Field(players, 4, array) ||
        !memory.Read(0x02214d00, selector) || !memory.Field(selector, 0x24, index) || index >= 4 ||
        !memory.Field(array, index * 4, roots.player)) return false;
    return true;
}
struct Collection {
    uint32_t completed{}, total{}, completedValue{};
};
inline std::optional<Collection> ReadCollection(const Memory& memory, uint32_t progress, uint32_t offset) {
    uint32_t handle{}, group{}, array{}; uint16_t count{};
    if (!memory.Field(progress, offset, handle) || !memory.Reference(handle, group) || !group ||
        !memory.Field(group, 0xc, array) || !memory.Read(group + 0x12, count)) return {};
    count &= 0x3fff;
    if (!count || count > 4096) return {};
    Collection result{}; result.total = count;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t childHandle{}, child{}, state{}, value{};
        if (!memory.Field(array, i * 4, childHandle) || !memory.Reference(childHandle, child) || !child ||
            !memory.Field(child, 0x10, state) || !memory.Field(child, 8, value)) return {};
        // 01123210 increments the native completed counter only for state 2.
        if (state == 2) {
            ++result.completed;
            if (value > UINT32_MAX - result.completedValue) return {};
            result.completedValue += value;
        }
    }
    uint32_t checkHandle{}, checkGroup{}, checkArray{}; uint16_t checkCount{};
    if (!memory.Field(progress, offset, checkHandle) || checkHandle != handle ||
        !memory.Reference(handle, checkGroup) || checkGroup != group ||
        !memory.Field(group, 0xc, checkArray) || checkArray != array ||
        !memory.Read(group + 0x12, checkCount) || (checkCount & 0x3fff) != count) return {};
    return result;
}
}

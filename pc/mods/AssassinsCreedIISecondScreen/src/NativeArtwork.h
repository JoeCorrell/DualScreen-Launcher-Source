#pragma once
#include "NativeRead.h"
#include <array>
namespace ac2ds::native {
// 012da080/012da0c0 and 017fc070 return handle+0c directly. The
// resource need not be resident: its ID is valid even when handle+0 is zero.
inline bool ReadArtworkId(const Memory& memory, uint32_t handle, uint32_t& id) {
    id = 0;
    if (!handle) return true;
    std::array<uint32_t,4> before{}, after{};
    if (!memory.Read(handle, before) || !memory.Read(handle, after) || before != after) return false;
    id = before[3]; return true;
}
}

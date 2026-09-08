#pragma once
#include <cstdint>
namespace ac2ds::native {
// Initial/invalidated domains refresh immediately. Recurring heavier domains
// use separate 250 ms slots, avoiding a single large once-per-second game stall.
inline bool RefreshDomain(uint64_t last,uint64_t now,unsigned slot,unsigned domain) {
    return !last || (slot==domain && now>=last && now-last>=1000);
}
}

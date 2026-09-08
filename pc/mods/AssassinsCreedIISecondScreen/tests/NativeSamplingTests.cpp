#include "NativeSampling.h"
#include <array>
#include <cassert>
#include <iostream>
int main() {
    using ac2ds::native::RefreshDomain;
    std::array<uint64_t,4> last{{0,0,0,0}};
    std::array<unsigned,4> counts{{0,0,0,0}};
    for(unsigned tick=0;tick<24;++tick) {
        uint64_t now=10000+tick*251;unsigned refreshed{};
        for(unsigned domain=0;domain<4;++domain)if(RefreshDomain(last[domain],now,tick%4,domain)) {
            last[domain]=now;++counts[domain];++refreshed;
        }
        assert(tick==0?refreshed==4:refreshed<=1);
    }
    for(auto count:counts)assert(count==6);
    // Transitions and explicit equipment invalidations bypass the slot delay.
    for(unsigned slot=0;slot<4;++slot)for(unsigned domain=0;domain<4;++domain)
        assert(RefreshDomain(0,17000,slot,domain));
    assert(!RefreshDomain(17000,16000,0,0));
    assert(!RefreshDomain(17000,17999,0,0));
    assert(RefreshDomain(17000,18000,0,0));
    std::cout<<"Heavy domains stagger after initial refresh; lifecycle and clock guards passed\n";
}

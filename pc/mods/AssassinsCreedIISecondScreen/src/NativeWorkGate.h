#pragma once
#include <atomic>

namespace ac2ds::native {
// Single outstanding sample; never accumulate catch-up work or wait on the
// game thread. The same gate serializes adapter caches with native actions.
class WorkGate {
    enum State { Idle, Pending, Reading, Acting };
    std::atomic<State> state_{Idle};
    bool Enter(State from, State to) {
        return state_.compare_exchange_strong(from,to,std::memory_order_acq_rel);
    }
public:
    bool Schedule() { return Enter(Idle,Pending); }
    bool BeginRead() { return Enter(Pending,Reading); }
    bool BeginAction() { return Enter(Idle,Acting); }
    void CancelPending() { Enter(Pending,Idle); }
    void Complete() { state_.store(Idle,std::memory_order_release); }
};
}

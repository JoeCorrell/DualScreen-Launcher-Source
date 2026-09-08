#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace c3probe {
static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free,
    "Research callbacks require lock-free scalar bookkeeping");
// Research callback bookkeeping only. An observation epoch invalidates copied
// observations after ANY tracked system event. It is NOT a level, save, actor,
// or lifecycle generation; those require separately verified transition rules.
// No memory allocation, IO, engine calls, waiting, or retry loops occur here.
class ProbeCallbackGuard {
    std::atomic<uint32_t> observationEpoch_{1},systemDepth_{},updateDepth_{},updateThread_{};
    std::atomic<bool> exhausted_{},threadConflict_{};
    uint32_t AdvanceEpoch() noexcept {
        const uint32_t previous=observationEpoch_.fetch_add(1,std::memory_order_acq_rel);
        if(previous==std::numeric_limits<uint32_t>::max()) exhausted_.store(true,std::memory_order_release);
        return previous+1;
    }
    void EnterDepth(std::atomic<uint32_t>& depth,uint32_t& previous) noexcept {
        previous=depth.fetch_add(1,std::memory_order_acq_rel);
        if(previous==std::numeric_limits<uint32_t>::max()) exhausted_.store(true,std::memory_order_release);
    }
    void LeaveDepth(std::atomic<uint32_t>& depth) noexcept {
        if(depth.fetch_sub(1,std::memory_order_acq_rel)==0) exhausted_.store(true,std::memory_order_release);
    }
public:
    // Non-default seed permits bounded overflow tests without billions of calls.
    explicit ProbeCallbackGuard(uint32_t initialObservationEpoch=1) noexcept
        : observationEpoch_(initialObservationEpoch), exhausted_(initialObservationEpoch==0) {}
    ProbeCallbackGuard(const ProbeCallbackGuard&)=delete;
    ProbeCallbackGuard& operator=(const ProbeCallbackGuard&)=delete;

    uint32_t ObservationEpoch() const noexcept { return observationEpoch_.load(std::memory_order_acquire); }
    uint32_t SystemDepth() const noexcept { return systemDepth_.load(std::memory_order_acquire); }
    uint32_t UpdateDepth() const noexcept { return updateDepth_.load(std::memory_order_acquire); }
    uint32_t ObservedUpdateThread() const noexcept { return updateThread_.load(std::memory_order_acquire); }
    bool Exhausted() const noexcept { return exhausted_.load(std::memory_order_acquire); }
    bool ThreadConflict() const noexcept { return threadConflict_.load(std::memory_order_acquire); }

    class SystemScope {
        ProbeCallbackGuard* owner_{};
        uint32_t entryEpoch_{};
    public:
        SystemScope(ProbeCallbackGuard& owner,bool track) noexcept {
            if(!track) return;
            owner_=&owner;
            uint32_t previous{}; owner_->EnterDepth(owner_->systemDepth_,previous);
            entryEpoch_=owner_->AdvanceEpoch();
        }
        ~SystemScope() noexcept { if(owner_) owner_->LeaveDepth(owner_->systemDepth_); }
        SystemScope(const SystemScope&)=delete;
        SystemScope& operator=(const SystemScope&)=delete;
        // Record THIS entry value in an event journal, not a later global load.
        // Nested/concurrent events can advance the global epoch before return.
        uint32_t EntryObservationEpoch() const noexcept { return entryEpoch_; }
        bool Tracked() const noexcept { return owner_!=nullptr; }
    };

    class UpdateScope {
        ProbeCallbackGuard* owner_{};
        uint32_t entryEpoch_{},thread_{};
        bool outermost_{},enteredWithoutSystem_{};
    public:
        UpdateScope(ProbeCallbackGuard& owner,uint32_t currentThread,bool track) noexcept {
            if(!track) return;
            owner_=&owner; thread_=currentThread;
            uint32_t previous{}; owner_->EnterDepth(owner_->updateDepth_,previous);
            outermost_=previous==0;
            enteredWithoutSystem_=owner_->SystemDepth()==0;
            entryEpoch_=owner_->ObservationEpoch();
            uint32_t expected=0;
            if(!thread_) owner_->threadConflict_.store(true,std::memory_order_release);
            else if(!owner_->updateThread_.compare_exchange_strong(expected,thread_,std::memory_order_acq_rel) &&
                    expected!=thread_) owner_->threadConflict_.store(true,std::memory_order_release);
        }
        ~UpdateScope() noexcept { if(owner_) owner_->LeaveDepth(owner_->updateDepth_); }
        UpdateScope(const UpdateScope&)=delete;
        UpdateScope& operator=(const UpdateScope&)=delete;
        uint32_t EntryObservationEpoch() const noexcept { return entryEpoch_; }
        bool Outermost() const noexcept { return outermost_; }
        // Call immediately before and after copied-value sampling, after the
        // original Update has returned. While any nested Update is executing,
        // even the outer scope is rejected. A thread conflict permanently
        // disarms observations; this helper never automatically re-attests it.
        // True is diagnostic bookkeeping permission, NOT thread/lifetime proof.
        bool CanObserve() const noexcept {
            return owner_ && outermost_ && enteredWithoutSystem_ && entryEpoch_ &&
                !owner_->Exhausted() && !owner_->ThreadConflict() &&
                owner_->ObservedUpdateThread()==thread_ && owner_->UpdateDepth()==1 &&
                owner_->SystemDepth()==0 && owner_->ObservationEpoch()==entryEpoch_;
        }
    };

    SystemScope EnterSystemEvent(bool track=true) noexcept { return SystemScope(*this,track); }
    UpdateScope EnterUpdate(uint32_t currentThread,bool track=true) noexcept { return UpdateScope(*this,currentThread,track); }
};
} // namespace c3probe

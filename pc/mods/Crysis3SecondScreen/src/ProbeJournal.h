#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace c3probe {
struct SystemRecord {
    uint64_t at{};
    uint32_t epoch{}, thread{}, event{}, arg0{}, arg1{};
};
// Bounded diagnostic-only queue. Game callbacks never wait or perform file IO.
class SystemJournal {
    std::atomic_flag busy_=ATOMIC_FLAG_INIT;
    std::array<SystemRecord,128> entries_{};
    size_t count_{};
    std::atomic<uint32_t> dropped_{};
public:
    void Push(const SystemRecord& record) noexcept {
        if(busy_.test_and_set(std::memory_order_acquire)) { ++dropped_; return; }
        if(count_==entries_.size()) ++dropped_;
        else entries_[count_++]=record;
        busy_.clear(std::memory_order_release);
    }
    size_t Drain(std::array<SystemRecord,128>& destination,uint32_t& dropped) noexcept {
        dropped=0;
        if(busy_.test_and_set(std::memory_order_acquire)) return 0;
        const size_t result=count_;
        for(size_t i=0;i<result;++i) destination[i]=entries_[i];
        count_=0; dropped=dropped_.exchange(0);
        busy_.clear(std::memory_order_release);
        return result;
    }
};
}

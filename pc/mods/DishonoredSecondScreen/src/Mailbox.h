#pragma once

#include "Bridge.h"
#include <atomic>
#include <cstring>
#include <memory>

namespace dishonored {

// SPSC triple buffer: game thread owns writing, panel owns reading. Exchanging
// the middle slot transfers ownership; neither side touches the other's slot.
// This avoids both a game-thread wait and a C++ data race disguised as a seqlock.
class SnapshotMailbox {
public:
    SnapshotMailbox() : slots_(new bridge::Snapshot[3]) {}
    SnapshotMailbox(const SnapshotMailbox&) = delete;
    SnapshotMailbox& operator=(const SnapshotMailbox&) = delete;
    bridge::Snapshot& BeginWrite() noexcept { return slots_[writer_]; }
    void Publish() noexcept {
        writer_ = middle_.exchange(writer_ | Dirty, std::memory_order_acq_rel) & Index;
    }
    const bridge::Snapshot& Read(bool& changed) noexcept {
        changed = (middle_.load(std::memory_order_acquire) & Dirty) != 0;
        if (changed) reader_ = middle_.exchange(reader_, std::memory_order_acq_rel) & Index;
        return slots_[reader_];
    }
private:
    static constexpr uint32_t Dirty = 4, Index = 3;
    std::unique_ptr<bridge::Snapshot[]> slots_;
    uint32_t writer_{0}, reader_{1};
    std::atomic<uint32_t> middle_{2};
};

// One producer, one consumer. Used separately for UI->game commands and
// game->UI acknowledgements. Overflow rejects a new action rather than
// overwriting an action or executing it without an acknowledgement slot.
template<class T, uint32_t Capacity> class Queue {
public:
    static_assert(Capacity > 1 && Capacity < 65536);
    static_assert((Capacity & (Capacity - 1)) == 0, "Unsigned counter wrap requires a power-of-two capacity");
    bool Push(const T& value) noexcept {
        const uint32_t write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) == Capacity) return false;
        values_[write % Capacity] = value;
        write_.store(write + 1, std::memory_order_release);
        return true;
    }
    bool Pop(T& value) noexcept {
        const uint32_t read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        value = values_[read % Capacity];
        read_.store(read + 1, std::memory_order_release);
        return true;
    }
    bool HasSpace() const noexcept {
        return write_.load(std::memory_order_relaxed) - read_.load(std::memory_order_acquire) < Capacity;
    }
private:
    std::array<T, Capacity> values_{};
    std::atomic<uint32_t> read_{0}, write_{0};
};

struct Bridge {
    enum class Bootstrap : uint8_t { Starting, Ready, Failed };
    enum class EventKind : uint8_t { Lifecycle, State, Domain, ActionSubmitted, ActionResult, MemoryFault, SampleReads, RootValidation, ObjectiveMarkers, MapDiscovery, UpperViewport, SamplePhase, MapProjection };
    struct Event {
        EventKind kind{};
        uint64_t at{}, generation{}, id{}, revision{};
        int32_t a{}, b{}, c{}, d{};
    };
    SnapshotMailbox snapshots;
    std::atomic<Bootstrap> bootstrap{Bootstrap::Starting}; // Initializer -> panel; never publishes engine pointers.
    std::atomic<bool> stackedDesktop{}; // Panel -> game: exact Thor topology, never a HWND or engine pointer.
    Queue<bridge::Command, 32> commands;
    Queue<bridge::Acknowledgement, 32> acknowledgements;
    Queue<Event, 128> diagnostics; // Game thread -> panel worker, no file I/O on tick.
    std::atomic<uint32_t> droppedDiagnostics{};
    void Record(const Event& event) noexcept {
        if (!diagnostics.Push(event)) droppedDiagnostics.fetch_add(1, std::memory_order_relaxed);
    }
};

} // namespace dishonored

#include "UiPerf.h"

#include <windows.h>

#include <atomic>

#include "Log.h"

namespace fo4ds {
namespace {

struct Aggregate {
  std::atomic<uint64_t> count{0};
  std::atomic<uint64_t> total_us{0};
  std::atomic<uint64_t> maximum_us{0};
};

Aggregate g_aggregates[static_cast<size_t>(PerfDomain::kCount)];
uint64_t g_next_flush_ms = 0;

const char* DomainName(PerfDomain domain) {
  switch (domain) {
    case PerfDomain::kGameTick: return "game_tick";
    case PerfDomain::kSnapshotPublish: return "snapshot_publish";
    case PerfDomain::kPanelCompose: return "panel_compose";
    case PerfDomain::kPanelPresent: return "panel_present";
    case PerfDomain::kBootstrapScan: return "bootstrap_scan";
    case PerfDomain::kInventoryScan: return "inventory_scan";
    case PerfDomain::kDataScan: return "data_scan";
    case PerfDomain::kMapScan: return "map_scan";
    case PerfDomain::kRadioScan: return "radio_scan";
    case PerfDomain::kEffectScan: return "effect_scan";
    case PerfDomain::kCount: break;
  }
  return "unknown";
}

}  // namespace

uint64_t PerfNow() {
  LARGE_INTEGER value = {};
  QueryPerformanceCounter(&value);
  return static_cast<uint64_t>(value.QuadPart);
}

uint64_t PerfElapsedMicros(uint64_t start) {
  LARGE_INTEGER frequency = {};
  LARGE_INTEGER now = {};
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&now);
  if (frequency.QuadPart <= 0 || static_cast<uint64_t>(now.QuadPart) < start)
    return 0;
  return (static_cast<uint64_t>(now.QuadPart) - start) * 1000000ull /
         static_cast<uint64_t>(frequency.QuadPart);
}

void RecordPerformance(PerfDomain domain, uint64_t microseconds) {
  const size_t index = static_cast<size_t>(domain);
  if (index >= static_cast<size_t>(PerfDomain::kCount)) return;
  Aggregate& aggregate = g_aggregates[index];
  aggregate.count.fetch_add(1, std::memory_order_relaxed);
  aggregate.total_us.fetch_add(microseconds, std::memory_order_relaxed);
  uint64_t maximum = aggregate.maximum_us.load(std::memory_order_relaxed);
  while (maximum < microseconds &&
         !aggregate.maximum_us.compare_exchange_weak(
             maximum, microseconds, std::memory_order_relaxed,
             std::memory_order_relaxed)) {}
}

void FlushPerformanceLog() {
  const uint64_t now = GetTickCount64();
  if (now < g_next_flush_ms) return;
  g_next_flush_ms = now + 10000;
  for (size_t i = 0; i < static_cast<size_t>(PerfDomain::kCount); ++i) {
    Aggregate& aggregate = g_aggregates[i];
    const uint64_t count = aggregate.count.exchange(0, std::memory_order_acq_rel);
    const uint64_t total = aggregate.total_us.exchange(0, std::memory_order_acq_rel);
    const uint64_t maximum = aggregate.maximum_us.exchange(0,
                                                            std::memory_order_acq_rel);
    if (!count) continue;
    Log(LogLevel::kInfo,
        "[PERF] domain=%s samples=%llu total_us=%llu avg_us=%llu max_us=%llu",
        DomainName(static_cast<PerfDomain>(i)),
        static_cast<unsigned long long>(count),
        static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(total / count),
        static_cast<unsigned long long>(maximum));
  }
}

}  // namespace fo4ds

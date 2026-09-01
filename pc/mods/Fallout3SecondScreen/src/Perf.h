#pragma once

#include <cstdint>

namespace fo3ds {

enum class PerfDomain : uint8_t {
  kGameTick,
  kGameTickGap,
  kSnapshotPublish,
  kPanelCompose,
  kPanelPresent,
  kUiAssetLoad,
  kBootstrapScan,
  kVitalsScan,
  kConditionScan,
  kSpecialScan,
  kSkillsScan,
  kGeneralStatsScan,
  kInventoryScan,
  kDataScan,
  kNoteScan,
  kMapScan,
  kLocalMapCapture,
  kMarkerScan,
  kRadioScan,
  kEffectScan,
  kCount,
};

uint64_t PerfNow();
uint64_t PerfElapsedMicros(uint64_t start);
void RecordPerformance(PerfDomain domain, uint64_t microseconds);
void FlushPerformanceLog();

class ScopedPerformance {
 public:
  explicit ScopedPerformance(PerfDomain domain)
      : domain_(domain), start_(PerfNow()) {}
  ~ScopedPerformance() {
    RecordPerformance(domain_, PerfElapsedMicros(start_));
  }

 private:
  PerfDomain domain_;
  uint64_t start_;
};

}  // namespace fo3ds
